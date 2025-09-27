#include "pch.h"

#include "bootloader_updater.h"
#include "crc_accelerator.h"

void checkBootloaderIntegrity() {
	efiPrintf("Current boot address: 0x%08x", getBootAddress());

	// Bootloader always lives in the first page of flash - FLASH_BASE
	bool bootloaderIntact = checkFirmwareImageIntegrity(FLASH_BASE);

	if (bootloaderIntact) {
		efiPrintf("Bootloader integrity check OK!");
	} else {
		efiPrintf("Bootloader integrity check failed - here be dragons!");
	}
}
