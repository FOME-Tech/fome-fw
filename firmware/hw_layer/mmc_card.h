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

BaseBlockDevice* initializeMmcBlockDevice();
void stopMmcBlockDevice();

// Initialize the SD card and mount its filesystem
// Returns true if the filesystem was successfully mounted for writing.
bool mountSdFilesystem();
void unmountSdFilesystem();

enum class SdCardState : uint8_t {
	// Still starting up: haven't yet decided who gets the card
	Initializing,
	// No card detected, or SD card disabled in the configuration
	NotPresent,
	// Card handed to the PC as a USB mass storage device, we can't touch it
	UsbMassStorage,
	// Filesystem mounted by the ECU
	Mounted,
	// Card detected, but the filesystem failed to mount, or was unmounted after a write error
	Failed,
};

SdCardState getSdCardState();

void onUsbConnectedNotifyMmcI();

// Implemented in firmware by reading from engineConfiguration
// Implemented in bootloader with defines (bootloader has no config!)
bool isSdCardEnabled();

#if HAL_USE_SPI
SPIDriver* getSdCardSpiDevice();
Gpio getSdCardCsPin();
#endif // HAL_USE_SPI

// FatFs writes (bytes / 512) sectors per f_write(), so we want as large a buffer as possible
#ifndef SD_LOG_BUFFER_SIZE
#define SD_LOG_BUFFER_SIZE 4096
#endif

#if EFI_PROD_CODE
struct SdLogBufferWriter final : public BufferedWriter<SD_LOG_BUFFER_SIZE> {
	bool failed = false;

	size_t writeInternal(const char* buffer, size_t count) override;
};
#endif // EFI_PROD_CODE
