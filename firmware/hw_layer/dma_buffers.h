/**
 * @file dma_buffers.h
 *
 * Combined DMA-safe memory region for all buffers that need cache-coherent
 * access on Cortex-M7 (SD card, big buffer, USB MSD block buffer).
 */

#pragma once

#include <cstdint>

#if EFI_PROD_CODE

#if EFI_FILE_LOGGING
#include "ff.h"

struct SdLogBufferWriter;
#endif // EFI_FILE_LOGGING

namespace dma_buffers {

// The embedded INI drive uses 512-byte blocks, independently of SD support.
inline constexpr uint32_t IniBlockSize = 512;

void initMpu();
uint8_t* bigBuffer();
#if HAL_USE_USB_MSD && EFI_FILE_LOGGING
uint8_t* sdCardBlockBuffer();
#endif

#if EFI_FILE_LOGGING
FATFS* fs();
FIL* logFileFd();
SdLogBufferWriter& logBuffer();
#endif // EFI_FILE_LOGGING

} // namespace dma_buffers

#endif // EFI_PROD_CODE
