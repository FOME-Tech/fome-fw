/*
 * @file    mmc_card.h
 *
 *
 * @date Dec 30, 2013
 * @author Kot_dnz
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#pragma once

#include "buffered_writer.h"

#include "ff.h"

// Initialize the SD card and mount its filesystem
// Returns true if the filesystem was successfully mounted for writing.
bool mountSdFilesystem();
void unmountSdFilesystem();

void onUsbConnectedNotifyMmcI();

struct SdLogBufferWriter final : public BufferedWriter<512> {
	bool failed = false;

	size_t writeInternal(const char* buffer, size_t count) override;
};

// These are to get objects that need to be in memory safe
// to access with the SD DMA controller
namespace sd_mem {
	FIL* getLogFileFd();
	SdLogBufferWriter& getLogBuffer();
}
