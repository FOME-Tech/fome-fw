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

void initMpu();
uint8_t* bigBuffer();
uint8_t* sdCardBlockBuffer();

#if EFI_FILE_LOGGING
FATFS* fs();
FIL* logFileFd();
SdLogBufferWriter& logBuffer();
#endif // EFI_FILE_LOGGING

} // namespace dma_buffers

#endif // EFI_PROD_CODE
