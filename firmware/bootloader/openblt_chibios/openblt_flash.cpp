#include "pch.h"
#include "flash_int.h"

extern "C" {
	#include "boot.h"
	#include "flash.h"
}

void FlashInit() { }

blt_bool FlashVerifyChecksum() {
	return intFlashIsErased(FlashGetUserProgBaseAddress(), 4) ? BLT_FALSE : BLT_TRUE;
	// return BLT_FALSE;
}

blt_addr FlashGetUserProgBaseAddress() {
	return 0x08008000;
}

blt_bool FlashWrite(blt_addr addr, blt_int32u len, blt_int8u *data) {
	return (FLASH_RETURN_SUCCESS == intFlashWrite(addr, (const char*)data, len)) ? BLT_TRUE : BLT_FALSE;
	
	return BLT_TRUE;
}

blt_bool FlashErase(blt_addr addr, blt_int32u len) {
	if (!intFlashIsErased(addr, len)) {
		return (FLASH_RETURN_SUCCESS == intFlashErase(addr, len)) ? BLT_TRUE : BLT_FALSE;
	}

	return BLT_TRUE;
}

blt_bool FlashDone(void) {
	return BLT_TRUE;
}

blt_bool FlashWriteChecksum() {
	return BLT_TRUE;
}
