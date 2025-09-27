#include "pch.h"

#include "bootloader_updater.h"
#include "crc_accelerator.h"

void checkBootloaderIntegrity() {
	// Bootloader always lives in the first page of flash - FLASH_BASE
	bool bootloaderIntact = checkFirmwareImageIntegrity(FLASH_BASE);

	if (bootloaderIntact) {
		efiPrintf("Bootloader integrity check OK!");
	} else {
		efiPrintf("Bootloader integrity check failed - here be dragons!");
	}
}
