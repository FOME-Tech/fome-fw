#include "pch.h"
#include "flash_int.h"

extern "C" {
	#include "boot.h"
	#include "flash.h"
}

blt_addr FlashGetUserProgBaseAddress() {
	return FLASH_BASE + 32 * 1024
}

blt_bool FlashWrite(blt_addr addr, blt_int32u len, blt_int8u *data) {
	// don't allow overwriting the bootloader
	if (addr < FlashGetUserProgBaseAddress()) {
		return BLT_FALSE;
	}

	return
		(FLASH_RETURN_SUCCESS == intFlashWrite(addr, (const char*)data, len))
		? BLT_TRUE
		: BLT_FALSE;
}

blt_bool FlashWriteChecksum() {
	return BLT_TRUE;
}
