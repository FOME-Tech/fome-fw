/**
 *
 * http://www.chibios.com/forum/viewtopic.php?f=8&t=820
 * https://github.com/tegesoft/flash-stm32f407
 *
 * @file    flash_int.c
 * @brief	Lower-level code related to internal flash memory
 */

#include "pch.h"

#if EFI_INTERNAL_FLASH

#include "flash_int.h"
#include <string.h>

// Use bank 2 on H7
#define FLASH_CR ((ctlr) ? FLASH->CR2 : FLASH->CR1)
#define FLASH_SR ((ctlr) ? FLASH->SR2 : FLASH->SR1)
#define FLASH_KEYR ((ctlr) ? FLASH->KEYR2 : FLASH->KEYR1)

// QW bit supercedes the older BSY bit
void intFlashWait(uint8_t ctlr, int waitFirstMs) {
	if (waitFirstMs) {
		chThdSleepMilliseconds(waitFirstMs);
	}

	while (FLASH_SR & FLASH_SR_QW) {
		chThdSleepMilliseconds(1);

		__DSB();
	}
}

flashaddr_t intFlashSectorBegin(flashsector_t sector) {
	flashaddr_t address = FLASH_BASE;
	while (sector > 0) {
		--sector;
		address += flashSectorSize(sector);
	}
	return address;
}

static void intFlashClearErrors(uint8_t ctlr) {
	// Both banks use write-one-to-clear registers. Parenthesize the selected
	// register so bank 2 receives the write as well.
	(ctlr ? FLASH->CCR2 : FLASH->CCR1) = FLASH_CCR_CLR_EOP | FLASH_CCR_CLR_WRPERR | FLASH_CCR_CLR_PGSERR |
										 FLASH_CCR_CLR_STRBERR | FLASH_CCR_CLR_INCERR | FLASH_CCR_CLR_OPERR |
										 FLASH_CCR_CLR_RDPERR | FLASH_CCR_CLR_RDSERR | FLASH_CCR_CLR_SNECCERR |
										 FLASH_CCR_CLR_DBECCERR | FLASH_CCR_CLR_CRCEND | FLASH_CCR_CLR_CRCRDERR;
}

static int intFlashCheckErrors(uint8_t ctlr) {
	uint32_t sr = FLASH_SR;

	if (sr & (FLASH_SR_OPERR | FLASH_SR_CRCRDERR)) {
		return FLASH_RETURN_OPERROR;
	}
	if (sr & (FLASH_SR_WRPERR | FLASH_SR_RDPERR | FLASH_SR_RDSERR)) {
		return FLASH_RETURN_WPERROR;
	}
	if (sr & FLASH_SR_STRBERR) {
		return FLASH_RETURN_PPARALLERROR;
	}
	if (sr & (FLASH_SR_PGSERR | FLASH_SR_INCERR)) {
		return FLASH_RETURN_PSEQERROR;
	}
	if (sr & (FLASH_SR_SNECCERR | FLASH_SR_DBECCERR)) {
		return FLASH_RETURN_BAD_FLASH;
	}

	return FLASH_RETURN_SUCCESS;
}

/**
 * @brief Unlock the flash memory for write access.
 * @return HAL_SUCCESS  Unlock was successful.
 * @return HAL_FAILED    Unlock failed.
 */
static bool intFlashUnlock(size_t ctlr) {
	/* Check if unlock is really needed */
	if (!(FLASH_CR & FLASH_CR_LOCK)) {
		return HAL_SUCCESS;
	}

	/* Write magic unlock sequence */
	FLASH_KEYR = 0x45670123;
	FLASH_KEYR = 0xCDEF89AB;

	/* Check if unlock was successful */
	if (FLASH_CR & FLASH_CR_LOCK) {
		return HAL_FAILED;
	}
	return HAL_SUCCESS;
}

/**
 * @brief Lock the flash memory for write access.
 */
#define intFlashLock()                                                                                                 \
	{ FLASH_CR |= FLASH_CR_LOCK; }

int intFlashSectorErase(flashsector_t sector) {
	int ret;
	uint8_t sectorRegIdx;

	uint8_t ctlr;

	if (sector >= 8) {
		// Use second bank's controller: convert to sector within the bank
		ctlr = 1;
		sectorRegIdx = sector - 8;
	} else {
		ctlr = 0;
		sectorRegIdx = sector;
	}

#ifndef EFI_BOOTLOADER
	efiPrintf("Flash: erase sector %d bank %d sectorRegIdx %d...", sector, ctlr ? 2 : 1, sectorRegIdx);
	Timer eraseTimer;
	eraseTimer.reset();
#endif

	/* Unlock flash for write access */
	if (intFlashUnlock(ctlr) == HAL_FAILED) {
		return FLASH_RETURN_NO_PERMISSION;
	}

	// Mitigation for https://github.com/FOME-Tech/fome-fw/issues/685
	struct ScopeCacheDisabler {
		ScopeCacheDisabler() {
			SCB_DisableICache();
			SCB_InvalidateICache();
		}

		~ScopeCacheDisabler() {
			SCB_EnableICache();
		}
	} cacheDisabler;

	/* Wait for any busy flags. */
	intFlashWait(ctlr, 1);

	/* Clearing error status bits.*/
	intFlashClearErrors(ctlr);

	// Reset voltage range & sector number
	FLASH_CR &= ~(FLASH_CR_PSIZE | FLASH_CR_SNB);

	/* Start deletion of sector.
	 * SNB(4:1) is defined as:
	 * 00000 sector 0
	 * 00001 sector 1
	 * ...
	 * 01011 sector 11 (the end of 1st bank, 1Mb border)
	 * 10000 sector 12 (start of 2nd bank)
	 * ...
	 * 11011 sector 23 (the end of 2nd bank, 2Mb border)
	 * others not allowed */
	FLASH_CR |= (FLASH_CR_SER | FLASH_CR_PSIZE_VALUE | (sectorRegIdx << FLASH_CR_SNB_Pos) | FLASH_CR_START);

	/* Wait until it's finished. */
	intFlashWait(ctlr, 1000);

	/* Sector erase flag does not clear automatically. */
	FLASH_CR &= ~FLASH_CR_SER;

	/* Lock flash again */
	intFlashLock();

	ret = intFlashCheckErrors(ctlr);
	if (ret != FLASH_RETURN_SUCCESS) {
		return ret;
	}

	/* Check deleted sector for errors */
	if (intFlashIsErased(intFlashSectorBegin(sector), flashSectorSize(sector)) == FALSE) {
		return FLASH_RETURN_BAD_FLASH; /* Sector is not empty despite the erase cycle! */
	}

#ifndef EFI_BOOTLOADER
	efiPrintf("Flash: erase done in %.2f sec", eraseTimer.getElapsedSeconds());
#endif

	/* Successfully deleted sector */
	return FLASH_RETURN_SUCCESS;
}

int intFlashWrite(flashaddr_t address, const char* buffer, size_t size) {
	constexpr size_t flashWordSize = 32;
	if (size == 0) {
		return FLASH_RETURN_SUCCESS;
	}
	if (address % flashWordSize != 0) {
		return FLASH_RETURN_ALIGNERROR;
	}
	if (!buffer || address < FLASH_BASE || address > FLASH_END) {
		return FLASH_RETURN_NO_PERMISSION;
	}
	// A transaction belongs to one bank controller. Validate the range before
	// adding addresses or rounding, including a partial final flash word.
	flashaddr_t bankEnd = address < FLASH_BANK2_BASE ? FLASH_BANK2_BASE - 1 : FLASH_END;
	if (size > bankEnd - address + 1) {
		return FLASH_RETURN_NO_PERMISSION;
	}
#ifndef EFI_BOOTLOADER
	efiPrintf("Flash: write %d bytes at 0x%08x", size, address);
	Timer writeTimer;
	writeTimer.reset();
#endif

	// Select the appropriate controller for this address
	flashsector_t sector = intFlashSectorAt(address);
	uint8_t ctlr = sector >= 8;

	/* Unlock flash for write access */
	if (intFlashUnlock(ctlr) == HAL_FAILED) {
		return FLASH_RETURN_NO_PERMISSION;
	}

	/* Wait for any busy flags */
	intFlashWait(ctlr, 1);

	/* Setup parallelism before program */
	FLASH_CR &= ~FLASH_CR_PSIZE_MASK;
	FLASH_CR |= FLASH_CR_PSIZE_VALUE;

	int result = FLASH_RETURN_SUCCESS;
	volatile uint32_t* pWrite = reinterpret_cast<volatile uint32_t*>(address);
	while (size != 0) {
		// H7 requires a whole 256-bit flash word. Copy only the caller's bytes
		// and pad the rest with erased data; the source need not be aligned.
		alignas(32) uint32_t flashWord[flashWordSize / sizeof(uint32_t)];
		memset(flashWord, 0xff, sizeof(flashWord));
		size_t count = size < flashWordSize ? size : flashWordSize;
		memcpy(flashWord, buffer, count);
		intFlashClearErrors(ctlr);
		/* Enter flash programming mode */
		FLASH_CR |= FLASH_CR_PG;

		// Flush pipelines
		__ISB();
		__DSB();

		// Write 32 bytes
		for (size_t i = 0; i < flashWordSize / sizeof(uint32_t); i++) {
			*pWrite++ = flashWord[i];
		}

		// Flush pipelines
		__ISB();
		__DSB();

		/* Wait for completion */
		intFlashWait(ctlr, 1);

		/* Exit flash programming mode */
		FLASH_CR &= ~FLASH_CR_PG;

		// Flush pipelines
		__ISB();
		__DSB();

		result = intFlashCheckErrors(ctlr);
		if (result != FLASH_RETURN_SUCCESS) {
			break;
		}
		buffer += count;
		size -= count;
	}

	/* Lock flash again */
	intFlashLock();

#ifndef EFI_BOOTLOADER
	efiPrintf("Flash: write done in %.2f sec", writeTimer.getElapsedSeconds());
#endif

	return result;
}

#endif /* EFI_INTERNAL_FLASH */
