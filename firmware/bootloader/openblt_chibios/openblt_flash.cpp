#include "pch.h"
#include "flash_int.h"

#include <rusefi/crc.h>

extern "C" {
	#include "boot.h"
	#include "flash.h"
}

void FlashInit() {
	// Flash already init by ChibiOS
}

blt_addr FlashGetUserProgBaseAddress() {
#ifdef STM32H7XX
	return 0x08020000;
#else // not STM32H7
	return 0x08008000;
#endif
}

// Returns the first address after the end of the user program
static blt_addr FlashGetUserLastAddress() {
	// maximum program size is 768k for BL + FW
	return 0x08000000 + 768 * 1024;
}

blt_bool FlashWrite(blt_addr addr, blt_int32u len, blt_int8u *data) {
	return (FLASH_RETURN_SUCCESS == intFlashWrite(addr, (const char*)data, len)) ? BLT_TRUE : BLT_FALSE;
}

static bool didEraseChecksum = false;

blt_bool FlashErase(blt_addr addr, blt_int32u len) {
	if (!didEraseChecksum) {
		didEraseChecksum = true;

		// The first time we run an erase of any kind, erase the checksum from flash.
		// We aren't guaranteed to independently erase this page (if the firmware image
		// is small, for example), so force an erase the first time we're asked to erase
		// something else.
		if (FLASH_RETURN_SUCCESS != (FlashGetUserLastAddress() - sizeof(int32_t), sizeof(int32_t))) {
			return BLT_FALSE;
		}
	}

	if (!intFlashIsErased(addr, len)) {
		return (FLASH_RETURN_SUCCESS == intFlashErase(addr, len)) ? BLT_TRUE : BLT_FALSE;
	}

	return BLT_TRUE;
}

blt_bool FlashDone() {
	return BLT_TRUE;
}

static uint32_t generateChecksum(blt_addr start, blt_addr end) {
	void* startPtr = reinterpret_cast<void*>(start);
	size_t size = end - start;

	return crc32(startPtr, size);
}

blt_bool FlashWriteChecksum() {
	blt_addr start = FlashGetUserProgBaseAddress();
	// don't include the checksum field in the checksum
	blt_addr end = FlashGetUserLastAddress() - sizeof(uint32_t);

	uint32_t checksum = generateChecksum(start, end);

	// Write the checksum in to flash!
	return FlashWrite((blt_addr)end, 4, reinterpret_cast<uint8_t*>(&checksum));
}

blt_bool FlashVerifyChecksum() {
	// Naive check: if the first block is blank, there's no code there
	if (intFlashIsErased(FlashGetUserProgBaseAddress(), 4)) {
		return BLT_FALSE;
	}

	// Now do the actual CRC check to ensure we didn't get stuck with a half-written firmware image
	blt_addr start = FlashGetUserProgBaseAddress();
	// don't include the checksum field in the checksum
	blt_addr end = FlashGetUserLastAddress() - sizeof(uint32_t);

	uint32_t calcChecksum = generateChecksum(start, end);
	uint32_t storedChecksum = *(reinterpret_cast<uint32_t*>(end) + 1);

	return calcChecksum == storedChecksum ? BLT_TRUE : BLT_FALSE;
}
