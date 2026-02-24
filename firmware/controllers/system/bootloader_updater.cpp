#include "pch.h"

#include "bootloader_updater.h"
#include "crc_accelerator.h"

// uint32_t is unsigned long on ARM, but this printf handles %x for 32-bit values
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat"

#if EFI_USE_OPENBLT && CORTEX_MODEL == 7
#include "flash_int.h"

// This header is generated during the build from the checksummed bootloader binary
#include "bootloader_image.h"

static const size_t checksumOffset = 0x1C;

static uint32_t getStoredCrc(const uint8_t* image) {
	return *reinterpret_cast<const uint32_t*>(image + checksumOffset);
}

static void flashBootloader() {
	// Verify the embedded image has a valid integrity check before we trust it
	if (!checkFirmwareImageIntegrity(reinterpret_cast<uintptr_t>(bootloader_image))) {
		efiPrintf("Bootloader update: embedded image integrity check FAILED, aborting");
		return;
	}

	uint32_t embeddedCrc = getStoredCrc(bootloader_image);
	uint32_t flashCrc = getStoredCrc(reinterpret_cast<const uint8_t*>(FLASH_BASE));
	bool currentBootloaderValid = checkFirmwareImageIntegrity(FLASH_BASE);

	if (currentBootloaderValid && embeddedCrc == flashCrc) {
		efiPrintf("Bootloader is up to date (CRC 0x%08x)", flashCrc);

		// Bootloader is valid, but make sure boot address points to it
		if (getBootAddress() != FLASH_BASE) {
			efiPrintf("Boot address was 0x%08x, correcting to bootloader", getBootAddress());
			postBootloaderUpdate();
		}

		return;
	}

	if (!currentBootloaderValid) {
		efiPrintf("Bootloader update: current bootloader is CORRUPT, updating...");
	} else {
		efiPrintf("Bootloader update: CRC mismatch (flash 0x%08x, embedded 0x%08x), updating...",
			flashCrc, embeddedCrc);
	}

	// Set boot address to firmware so MCU stays bootable during the update
	preBootloaderUpdate();

	// Erase the bootloader flash sector(s)
	int eraseResult = intFlashErase(FLASH_BASE, bootloader_image_len);
	if (eraseResult != FLASH_RETURN_SUCCESS) {
		efiPrintf("Bootloader update: erase FAILED (%d)", eraseResult);
		// Boot address stays at firmware - MCU is still bootable
		return;
	}

	// Write the embedded bootloader image
	int writeResult = intFlashWrite(FLASH_BASE, reinterpret_cast<const char*>(bootloader_image), bootloader_image_len);
	if (writeResult != FLASH_RETURN_SUCCESS) {
		efiPrintf("Bootloader update: write FAILED (%d)", writeResult);
		// Boot address stays at firmware - MCU is still bootable
		return;
	}

	// Verify the written data matches
	if (!intFlashCompare(FLASH_BASE, reinterpret_cast<const char*>(bootloader_image), bootloader_image_len)) {
		efiPrintf("Bootloader update: verify FAILED - data mismatch after write");
		// Boot address stays at firmware - MCU is still bootable
		return;
	}

	// Verify the written image passes integrity check
	if (!checkFirmwareImageIntegrity(FLASH_BASE)) {
		efiPrintf("Bootloader update: verify FAILED - integrity check failed after write");
		// Boot address stays at firmware - MCU is still bootable
		return;
	}

	// Success - restore boot address to bootloader
	postBootloaderUpdate();
	efiPrintf("Bootloader update: SUCCESS (new CRC 0x%08x)", embeddedCrc);
}
#endif // EFI_USE_OPENBLT && CORTEX_MODEL == 7

void updateBootloader() {
#if CORTEX_MODEL == 7
	efiPrintf("Current boot address: 0x%08x", getBootAddress());
#endif

	// Bootloader always lives in the first page of flash - FLASH_BASE
	bool bootloaderIntact = checkFirmwareImageIntegrity(FLASH_BASE);

	if (bootloaderIntact) {
		efiPrintf("Bootloader integrity check OK!");
	} else {
		efiPrintf("Bootloader integrity check failed - here be dragons!");
	}

#if EFI_USE_OPENBLT && CORTEX_MODEL == 7
	flashBootloader();
#endif
}

#pragma GCC diagnostic pop
