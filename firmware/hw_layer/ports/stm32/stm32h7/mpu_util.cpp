/**
 * @file	mpu_util.cpp
 *
 * @date Feb 26, 2021
 * @author Matthew Kennedy, (c) 2021
 */

#include "pch.h"

#include "flash_int.h"
#include "stm32h7xx_hal_flash.h"

uintptr_t getBootAddress() {
	FLASH_OBProgramInitTypeDef flashData;
	HAL_FLASHEx_OBGetConfig(&flashData);

	// H7 HAL returns the real address directly
	return flashData.BootAddr0;
}

bool setBootAddress(uintptr_t address) {
	if ((address & 0xFFFF) != 0) {
		// H7 boot address must be 64KB aligned
		return false;
	}

	FLASH_OBProgramInitTypeDef flashData;
	flashData.BootConfig = OB_BOOT_ADD0;
	flashData.OptionType = OPTIONBYTE_BOOTADD;
	flashData.BootAddr0 = address;

	HAL_FLASH_OB_Unlock();
	HAL_FLASHEx_OBProgram(&flashData);
	HAL_StatusTypeDef status = HAL_FLASH_OB_Launch();
	HAL_FLASH_OB_Lock();

	return status == HAL_OK;
}

void preBootloaderUpdate() {
	setBootAddress(SCB->VTOR);
	efiPrintf("Boot address set to firmware: 0x%08x", (uintptr_t)SCB->VTOR);
}

void postBootloaderUpdate() {
	setBootAddress(FLASH_BASE);
	efiPrintf("Boot address restored to bootloader: 0x%08x", (uintptr_t)FLASH_BASE);
}

bool allowFlashWhileRunning() {
	// We only support dual bank H7, so always allow flash while running.
	return true;
}

size_t flashSectorSize(flashsector_t) {
	// All sectors on H7 are 128k
	return 128 * 1024;
}

uintptr_t getFlashAddrFirstCopy() {
	return 0x08100000;
}

uintptr_t getFlashAddrSecondCopy() {
	// Second copy is one sector past the first
	return getFlashAddrFirstCopy() + 128 * 1024;
}
