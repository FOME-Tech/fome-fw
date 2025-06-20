#include "pch.h"
#include "flash_int.h"

extern "C" {
	#include "boot.h"
	#include "flash.h"
}

blt_addr FlashGetUserProgBaseAddress() {
	return FLASH_BASE + 128 * 1024;
}

static uint8_t buffer[1024];
static size_t flashWriteSize = 0;
static blt_addr flashWriteAddr = 0;

static blt_bool flush() {
	if (flashWriteAddr && flashWriteSize) {
		// round up to next 32 byte
		auto residue = flashWriteSize % 32;
		if (residue != 0) {
			flashWriteSize += 32 - residue;
		}

		blt_bool result = (FLASH_RETURN_SUCCESS == intFlashWrite(flashWriteAddr, (const char*)buffer, flashWriteSize))
							? BLT_TRUE
							: BLT_FALSE;

		flashWriteSize = 0;
		flashWriteAddr = 0;

		return result;
	}

	return BLT_TRUE;
}

blt_bool FlashWrite(blt_addr addr, blt_int32u len, blt_int8u *data) {
	// don't allow overwriting the bootloader
	if (addr < FlashGetUserProgBaseAddress()) {
		return BLT_FALSE;
	}

	if (flashWriteSize + len > sizeof(buffer)) {
		// this write would overflow
		return BLT_FALSE;
	}

	// We just flushed (or init), store the flash write address
	if (!flashWriteSize) {
		flashWriteAddr = addr;
		memset(buffer, 0xFF, sizeof(buffer));
	}

	// Check that this write is contiguous with the last write
	if (flashWriteAddr + flashWriteSize != addr) {
		// This is a non-contiguous write
		return BLT_FALSE;
	}

	// Copy in to the buffer
	memcpy(buffer + flashWriteSize, data, len);
	flashWriteSize += len;

	// If full, flush
	if (flashWriteSize == sizeof(buffer)) {
		return flush();
	}

	return BLT_TRUE;
}

blt_bool FlashWriteChecksum() {
	return flush();
}
