/**
 * @file	mmc_card.cpp
 *
 * @date Dec 28, 2013
 * @author Kot_dnz
 * @author Andrey Belomutskiy, (c) 2012-2020
 *
 */

#include "pch.h"

#if EFI_FILE_LOGGING && EFI_PROD_CODE

#include "mmc_card.h"

#if HAL_USE_MMC_SPI
// Don't re-read SD card spi device after boot - it could change mid transaction (TS thread could preempt),
// which will cause disaster (usually multiple-unlock of the same mutex in UNLOCK_SD_SPI)
static SPIDriver* mmcSpiDevice = nullptr;

// MMC/SD driver instance.
MMCDriver MMCD1;

// MMC/SD over SPI driver configuration
static MMCConfig mmccfg = { NULL, &mmc_ls_spicfg, &mmc_hs_spicfg };

#define LOCK_SD_SPI spiAcquireBus(mmcSpiDevice)
#define UNLOCK_SD_SPI spiReleaseBus(mmcSpiDevice)

#endif /* HAL_USE_MMC_SPI */

#if !EFI_BOOTLOADER
// On STM32H7, these objects need their own MPU region if using SDMMC1
struct {
	struct {
		FATFS fs;
		FIL file;
		SdLogBufferWriter logBuffer;
	} usedPart;

	static_assert(sizeof(usedPart) <= 2048);

	// Fill the struct out to a full MPU region
	uint8_t padding[2048 - sizeof(usedPart)];
} mmcCardCacheControlledStorage SDMMC_MEMORY(2048);

namespace sd_mem {
FATFS* getFs() {
	return &mmcCardCacheControlledStorage.usedPart.fs;
}

FIL* getLogFileFd() {
	return &mmcCardCacheControlledStorage.usedPart.file;
}

SdLogBufferWriter& getLogBuffer() {
	return mmcCardCacheControlledStorage.usedPart.logBuffer;
}
} // namespace sd_mem

#endif // !EFI_BOOTLOADER

#if HAL_USE_MMC_SPI
/*
 * Attempts to initialize the MMC card.
 * Returns a BaseBlockDevice* corresponding to the SD card if successful, otherwise nullptr.
 */
BaseBlockDevice* initializeMmcBlockDevice() {
	#if !EFI_BOOTLOADER
		// Don't try to mount SD card in case of fatal error - hardware may be in an unexpected state
		if (hasFirmwareError()) {
			return nullptr;
		}
	#endif // EFI_BOOTLOADER

	mmcSpiDevice = getSdCardSpiDevice();

	if (!isSdCardEnabled() || !mmcSpiDevice) {
		return nullptr;
	}

	// todo: reuse initSpiCs method?
	mmc_hs_spicfg.ssport = mmc_ls_spicfg.ssport = getHwPort("mmc", getSdCardCsPin());
	mmc_hs_spicfg.sspad = mmc_ls_spicfg.sspad = getHwPin("mmc", getSdCardCsPin());
	mmccfg.spip = mmcSpiDevice;

	// Invalid SPI device, abort.
	if (!mmccfg.spip) {
		return nullptr;
	}

	// We think we have everything for the card, let's try to mount it!
	mmcObjectInit(&MMCD1);
	mmcStart(&MMCD1, &mmccfg);

	// Performs the initialization procedure on the inserted card.
	LOCK_SD_SPI;
	if (mmcConnect(&MMCD1) != HAL_SUCCESS) {
		efiPrintf("SD card (SPI) failed to connect");
		UNLOCK_SD_SPI;
		return nullptr;
	}
	// We intentionally never unlock in case of success, we take exclusive access of that spi device for SD use

	return reinterpret_cast<BaseBlockDevice*>(&MMCD1);
}

void stopMmcBlockDevice() {
	mmcDisconnect(&MMCD1);						// Brings the driver in a state safe for card removal.
	mmcStop(&MMCD1);							// Disables the MMC peripheral.
	UNLOCK_SD_SPI;
}
#endif /* HAL_USE_MMC_SPI */

// Some ECUs are wired for SDIO/SDMMC instead of SPI
#ifdef EFI_SDC_DEVICE
static const SDCConfig sdcConfig = {
	SDC_MODE_4BIT
};

BaseBlockDevice* initializeMmcBlockDevice() {
	if (!isSdCardEnabled()) {
		return nullptr;
	}

	sdcStart(&EFI_SDC_DEVICE, &sdcConfig);
	if (sdcConnect(&EFI_SDC_DEVICE) != HAL_SUCCESS) {
		efiPrintf("SD card (SDMMC) failed to connect");
		return nullptr;
	}

	// STM32H7 SDMMC1 needs the filesystem object to be in AXI
	// SRAM, but excluded from the cache
	#if defined(STM32H7XX) && !EFI_BOOTLOADER
	{
		void* base = &mmcCardCacheControlledStorage;
		static_assert(sizeof(mmcCardCacheControlledStorage) == 2048);
		uint32_t size = MPU_RASR_SIZE_2K;

		mpuConfigureRegion(MPU_REGION_5,
						base,
						MPU_RASR_ATTR_AP_RW_RW |
						MPU_RASR_ATTR_NON_CACHEABLE |
						MPU_RASR_ATTR_S |
						size |
						MPU_RASR_ENABLE);
		mpuEnable(MPU_CTRL_PRIVDEFENA);

		/* Invalidating data cache to make sure that the MPU settings are taken
		immediately.*/
		SCB_CleanInvalidateDCache();
	}
	#endif

	return reinterpret_cast<BaseBlockDevice*>(&EFI_SDC_DEVICE);
}

void stopMmcBlockDevice() {
	sdcDisconnect(&EFI_SDC_DEVICE);
	sdcStop(&EFI_SDC_DEVICE);
}

#endif /* EFI_SDC_DEVICE */

#endif // EFI_FILE_LOGGING && EFI_PROD_CODE
