extern "C" {
	#include "boot.h"
	#include "flash.h"
}

void FlashInit() { }

blt_bool FlashVerifyChecksum() {
	return BLT_TRUE;
}

blt_addr FlashGetUserProgBaseAddress() {
	return 0x08008000;
}

blt_bool FlashWrite(blt_addr addr, blt_int32u len, blt_int8u *data) {
	return BLT_TRUE;
}

blt_bool FlashErase(blt_addr addr, blt_int32u len) {
	return BLT_TRUE;
}

blt_bool FlashDone(void) {
	return BLT_TRUE;
}

blt_bool FlashWriteChecksum() {
	return BLT_TRUE;
}
