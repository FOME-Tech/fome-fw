#include "pch.h"
#include "flash_int.h"

extern "C" {
#include "boot.h"
#include "flash.h"
}

blt_addr FlashGetUserProgBaseAddress() {
#ifdef EFI_BOOTLOADER
	return FLASH_BASE + 128 * 1024;
#endif
}

#define FLASH_INVALID_ADDRESS 0xffffffff
#define FLASH_WRITE_BLOCK_SIZE 1024

struct FlashBlockInfo {
	blt_addr base_addr = FLASH_INVALID_ADDRESS;
	blt_int8u data[FLASH_WRITE_BLOCK_SIZE];
};

static blt_bool FlashWriteBlock(FlashBlockInfo& block) {
	blt_int8u* src = &block.data[0];
	auto dst = block.base_addr;
	size_t len = FLASH_WRITE_BLOCK_SIZE;

	if (FLASH_RETURN_SUCCESS != intFlashWrite(dst, reinterpret_cast<const char*>(src), len)) {
		return BLT_FALSE;
	}

	// compare written data vs. expected
	for (size_t i = 0; i < len; i++) {
		if (src[i] != ((blt_int8u*)dst)[i]) {
			return BLT_FALSE;
		}
	}

	return BLT_TRUE;
}

static blt_bool FlashInitBlock(FlashBlockInfo& block, blt_addr address) {
	// enforce address alignment
	if (address % FLASH_WRITE_BLOCK_SIZE != 0) {
		return BLT_FALSE;
	}

	if (block.base_addr != address) {
		block.base_addr = address;
		CpuMemCopy((blt_addr)block.data, address, FLASH_WRITE_BLOCK_SIZE);
	}

	return BLT_TRUE;
}

static blt_bool FlashSwitchBlock(FlashBlockInfo& block, blt_addr base_addr) {
	// Program the current block
	if (BLT_FALSE == FlashWriteBlock(block)) {
		return BLT_FALSE;
	}

	// Init the new block
	return FlashInitBlock(block, base_addr);
}

static blt_bool FlashAddToBlock(FlashBlockInfo& block, blt_addr address, blt_int8u* data, blt_int32u len) {
	blt_addr current_base_addr = (address / FLASH_WRITE_BLOCK_SIZE) * FLASH_WRITE_BLOCK_SIZE;

	// Initialize the block info if necessary
	if (block.base_addr == FLASH_INVALID_ADDRESS) {
		if (BLT_FALSE == FlashInitBlock(block, current_base_addr)) {
			return BLT_FALSE;
		}
	} else if (block.base_addr != current_base_addr) {
		// If the current data is for a different block,
		// program the current one and switch to the new one
		FlashSwitchBlock(block, current_base_addr);
	}

	// offset destination in to the middle of the block if necessary
	blt_int8u* dst = &(block.data[address - block.base_addr]);
	blt_int8u* src = data;

	do {
		// Keep the watchdog happy
		CopService();

		// We've reached the end of the block, flush it
		if ((dst - &block.data[0]) >= FLASH_WRITE_BLOCK_SIZE) {
			if (BLT_FALSE == FlashSwitchBlock(block, current_base_addr + FLASH_WRITE_BLOCK_SIZE)) {
				return BLT_FALSE;
			}

			dst = &block.data[0];
		}

		// Write in to the buffer, move pointers
		*dst = *src;
		dst++;
		src++;
		len--;
	} while (len > 0);

	return BLT_TRUE;
}

static FlashBlockInfo block;

blt_bool FlashWrite(blt_addr addr, blt_int32u len, blt_int8u* data) {
	// don't allow overwriting the bootloader
	if (addr < FlashGetUserProgBaseAddress()) {
		return BLT_FALSE;
	}

	return FlashAddToBlock(block, addr, data, len);
}

blt_bool FlashWriteChecksum() {
	if (block.base_addr != FLASH_INVALID_ADDRESS) {
		return FlashWriteBlock(block);
	}

	return BLT_TRUE;
}
