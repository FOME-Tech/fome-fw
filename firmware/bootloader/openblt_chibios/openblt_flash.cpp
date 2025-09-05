#include "pch.h"
#include "flash_int.h"
#include "crc_accelerator.h"

extern "C" {
	#include "boot.h"
	#include "flash.h"
}

void FlashInit() {
	// Flash already init by ChibiOS
}

blt_bool FlashErase(blt_addr addr, blt_int32u len) {
	// don't allow erasing the bootloader
	if (addr < FlashGetUserProgBaseAddress()) {
		return BLT_FALSE;
	}

	if (intFlashIsErased(addr, len)) {
		// Already blank, we can skip the expensive erase operation
		return BLT_TRUE;
	}

	return
		(FLASH_RETURN_SUCCESS == intFlashErase(addr, len))
		? BLT_TRUE
		: BLT_FALSE;
}

blt_bool FlashDone() {
	return BLT_TRUE;
}

blt_bool FlashVerifyChecksum() {
	// Naive check: if the first block is blank, there's no code there
	if (intFlashIsErased(FlashGetUserProgBaseAddress(), 4)) {
		return BLT_FALSE;
	}

	static const size_t checksumOffset = 0x1C;

	// Now do the actual CRC check to ensure we didn't get stuck with a half-written firmware image
	uint8_t* start = reinterpret_cast<uint8_t*>(FlashGetUserProgBaseAddress());

	size_t imageSize = *reinterpret_cast<size_t*>(start + checksumOffset + 4);

	if (imageSize > 1024 * 1024) {
		// impossibly large size, invalid
		return BLT_FALSE;
	}

	// part before checksum+size
	Crc crc(imageSize);
	crc.addData(start, checksumOffset);

	// part after checksum+size
	crc.addData(start + checksumOffset + 4, imageSize - (checksumOffset + 4));

	uint32_t storedChecksum = *reinterpret_cast<uint32_t*>(start + checksumOffset);

	return (crc.getCrc() == storedChecksum) ? BLT_TRUE : BLT_FALSE;
}
