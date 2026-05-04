#include "pch.h"

#if EFI_PROD_CODE

#include "dma_buffers.h"
#include "big_buffer.h"

#if EFI_FILE_LOGGING
#include "mmc_card.h"
#endif

static constexpr size_t nextPowerOf2(size_t v) {
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v++;
	return v;
}

static constexpr uint32_t computeLog2(size_t v) {
	uint32_t r = 0;
	while (v > 1) {
		v >>= 1;
		r++;
	}
	return r;
}

struct DmaBufferContents {
	uint8_t bigBuffer[BIG_BUFFER_SIZE];

#if EFI_FILE_LOGGING
	FATFS fs;
	FIL file;
	SdLogBufferWriter logBuffer;
#endif

#if HAL_USE_USB_MSD
	uint8_t sdBlockBuffer[MMCSD_BLOCK_SIZE];
#endif
};

static constexpr size_t DMA_REGION_SIZE = nextPowerOf2(sizeof(DmaBufferContents));

static struct {
	DmaBufferContents contents;
	uint8_t padding[DMA_REGION_SIZE - sizeof(DmaBufferContents)];
} dmaBufferRegion DMA_BUFFER_MEMORY(DMA_REGION_SIZE);

namespace dma_buffers {

void initMpu() {
#if defined(STM32F7XX) || defined(STM32H7XX)
	static constexpr uint32_t mpuRasrSize = (computeLog2(DMA_REGION_SIZE) - 1) << 1;

	mpuConfigureRegion(
			MPU_REGION_5,
			&dmaBufferRegion,
			MPU_RASR_ATTR_AP_RW_RW | MPU_RASR_ATTR_NON_CACHEABLE | MPU_RASR_ATTR_S | mpuRasrSize | MPU_RASR_ENABLE);
	mpuEnable(MPU_CTRL_PRIVDEFENA);

	SCB_CleanInvalidateDCache();
#endif
}

uint8_t* bigBuffer() {
	return dmaBufferRegion.contents.bigBuffer;
}

uint8_t* sdCardBlockBuffer() {
#if HAL_USE_USB_MSD
	return dmaBufferRegion.contents.sdBlockBuffer;
#else
	return nullptr;
#endif
}

#if EFI_FILE_LOGGING
FATFS* fs() {
	return &dmaBufferRegion.contents.fs;
}

FIL* logFileFd() {
	return &dmaBufferRegion.contents.file;
}

SdLogBufferWriter& logBuffer() {
	return dmaBufferRegion.contents.logBuffer;
}
#endif // EFI_FILE_LOGGING

} // namespace dma_buffers

#endif // EFI_PROD_CODE
