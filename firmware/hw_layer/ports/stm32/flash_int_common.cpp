// Common logic for flash memory between F4/F7 and H7 implementations

#include "pch.h"

#if EFI_INTERNAL_FLASH

#include "flash_int.h"
#include "mpu_regions.h"

// The MPU region over the tune copies, once intFlashInitMpu() has set it up
static FlashConfigRegion mpuRegion = {0, 0};

// One MPU region over [base, base + size): strongly ordered, XN, with access bits ap. Strongly
// ordered memory faults on unaligned accesses: every reader of these is word aligned (or reads bytes).
static bool configureFlashRegion(uint32_t region, uintptr_t base, size_t size, uint32_t ap) {
	bool isPowerOf2 = size >= 32 && (size & (size - 1)) == 0;
	if (!isPowerOf2 || base % size != 0) {
		// Not expressible as one MPU region: leave it unprotected
		return false;
	}

	// MPU_RASR_SIZE(n) is a region of 2^(n+1) bytes
	uint32_t sizeBits = MPU_RASR_SIZE(__builtin_ctz(size) - 1);
	mpuConfigureRegion(
			region, base, ap | MPU_RASR_ATTR_XN | MPU_RASR_ATTR_STRONGLY_ORDERED | sizeBits | MPU_RASR_ENABLE);
	return true;
}

void intFlashInitMpu() {
	// System memory: the ST bootloader, OTP, UID, flash size and option bytes, at 0x1FFxxxxx on every
	// chip (F4 0x1FFF0000, F7 0x1FF00000, H7 0x1FF00000 and 0x1FF40000). Read-only, since the UID is
	// read for the USB serial number and the flash size for the layout. Jumping to the ST DFU
	// bootloader happens after a reset, before this runs. On H7 a speculative read here is flagged
	// as a read protection or secure error (RDPERR/RDSERR), which fails the flash erase or write in
	// progress.
	configureFlashRegion(MPU_REGION_SYSTEM_MEMORY, 0x1FF00000, 1024 * 1024, MPU_RASR_ATTR_AP_RO_RO);

	FlashConfigRegion region = getFlashConfigRegion();
	if (configureFlashRegion(MPU_REGION_FLASH_CONFIG, region.base, region.size, MPU_RASR_ATTR_AP_NA_NA)) {
		mpuRegion = region;
	}

	mpuEnable(MPU_CTRL_PRIVDEFENA);
	__DSB();
	__ISB();
#if CORTEX_MODEL == 7
	// Strongly ordered memory isn't cacheable: drop any lines already cached from it. Only with the
	// D-cache on (not in the bootloader): cleaning a cache that was never invalidated writes back
	// whatever it powered up holding.
	if (SCB->CCR & SCB_CCR_DC_Msk) {
		SCB_CleanInvalidateDCache();
	}
#endif
}

// Change only the region's access bits. Selected and written with the kernel locked, since a
// context switch reprograms the MPU too (stack guard pages). Returns the previous access bits.
static uint32_t setConfigFlashAccess(uint32_t ap) {
	syssts_t sts = chSysGetStatusAndLockX();
	__DSB();
	MPU->RNR = MPU_REGION_FLASH_CONFIG;
	uint32_t rasr = MPU->RASR;
	MPU->RASR = (rasr & ~MPU_RASR_ATTR_AP_MASK) | ap;
	__DSB();
	__ISB();
	chSysRestoreStatusX(sts);
	return rasr & MPU_RASR_ATTR_AP_MASK;
}

static uint32_t accessBits(FlashAccess access) {
	switch (access) {
		case FlashAccess::Read:
			return MPU_RASR_ATTR_AP_RO_RO;
		case FlashAccess::ReadWrite:
			return MPU_RASR_ATTR_AP_RW_RW;
		default:
			return MPU_RASR_ATTR_AP_NA_NA;
	}
}

ScopeFlashAccess::ScopeFlashAccess(flashaddr_t address, size_t size, FlashAccess access)
	: m_active(
			  mpuRegion.size != 0 && size != 0 && address < mpuRegion.base + mpuRegion.size &&
			  address + size > mpuRegion.base) {
	if (m_active) {
		m_previousAp = setConfigFlashAccess(accessBits(access));
	}
}

ScopeFlashAccess::~ScopeFlashAccess() {
	if (m_active) {
		setConfigFlashAccess(m_previousAp);
	}
}

flashaddr_t intFlashSectorEnd(flashsector_t sector) {
	return intFlashSectorBegin(sector + 1);
}

flashsector_t intFlashSectorAt(flashaddr_t address) {
	flashsector_t sector = 0;
	while (address >= intFlashSectorEnd(sector)) {
		++sector;
	}
	return sector;
}

int intFlashErase(flashaddr_t address, size_t size) {
	while (size > 0) {
		flashsector_t sector = intFlashSectorAt(address);
		int err = intFlashSectorErase(sector);
		if (err != FLASH_RETURN_SUCCESS) {
			return err;
		}
		address = intFlashSectorEnd(sector);
		size_t sector_size = flashSectorSize(sector);
		if (sector_size >= size) {
			break;
		}
		size -= sector_size;
	}

	return FLASH_RETURN_SUCCESS;
}

bool intFlashIsErased(flashaddr_t address, size_t size) {
	ScopeFlashAccess read(address, size, FlashAccess::Read);

#if CORTEX_MODEL == 7 && !defined(EFI_BOOTLOADER)
	// If we have a cache, invalidate the relevant cache lines.
	// They may still contain old data, leading us to believe that the
	// flash erase failed.
	SCB_InvalidateDCache_by_Addr((uint32_t*)address, size);
#endif

	/* Check for default set bits in the flash memory
	 * For efficiency, compare flashdata_t values as much as possible,
	 * then, fallback to byte per byte comparison. */
	while (size >= sizeof(flashdata_t)) {
		if (*(volatile flashdata_t*)address != (flashdata_t)(-1)) { // flashdata_t being unsigned, -1 is 0xFF..FF
			return false;
		}
		address += sizeof(flashdata_t);
		size -= sizeof(flashdata_t);
	}
	while (size > 0) {
		if (*(char*)address != 0xFF) {
			return false;
		}
		++address;
		--size;
	}

	return TRUE;
}

bool intFlashCompare(flashaddr_t address, const char* buffer, size_t size) {
	ScopeFlashAccess read(address, size, FlashAccess::Read);

	/* For efficiency, compare flashdata_t values as much as possible,
	 * then, fallback to byte per byte comparison. */
	while (size >= sizeof(flashdata_t)) {
		if (*(volatile flashdata_t*)address != *(flashdata_t*)buffer) {
			return FALSE;
		}
		address += sizeof(flashdata_t);
		buffer += sizeof(flashdata_t);
		size -= sizeof(flashdata_t);
	}
	while (size > 0) {
		if (*(volatile char*)address != *buffer) {
			return FALSE;
		}
		++address;
		++buffer;
		--size;
	}

	return TRUE;
}

int intFlashRead(flashaddr_t source, char* destination, size_t size) {
	ScopeFlashAccess read(source, size, FlashAccess::Read);

#if CORTEX_MODEL == 7 && !defined(EFI_BOOTLOADER)
	// If we have a cache, invalidate the relevant cache lines.
	// They may still contain old data, leading us to read invalid data.
	SCB_InvalidateDCache_by_Addr((uint32_t*)source, size);
#endif

	memcpy(destination, (char*)source, size);
	return FLASH_RETURN_SUCCESS;
}

#endif // EFI_INTERNAL_FLASH
