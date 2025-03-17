/**
 * @file	mmc_card.cpp
 *
 * @date Dec 28, 2013
 * @author Kot_dnz
 * @author Andrey Belomutskiy, (c) 2012-2020
 *
 */

#include "pch.h"
#include "mmc_card.h"


#if EFI_FILE_LOGGING && EFI_PROD_CODE

static bool fs_ready = false;

#include "ff.h"
#include "mass_storage_init.h"

#define SD_STATE_INIT "init"
#define SD_STATE_MOUNTED "MOUNTED"
#define SD_STATE_MOUNT_FAILED "MOUNT_FAILED"
#define SD_STATE_NOT_INSERTED "NOT_INSERTED"
#define SD_STATE_CONNECTING "CONNECTING"
#define SD_STATE_MSD "MSD"
#define SD_STATE_NOT_CONNECTED "NOT_CONNECTED"
#define SD_STATE_MMC_FAILED "MMC_CONNECT_FAILED"

// todo: shall we migrate to enum with enum2string for consistency? maybe not until we start reading sdStatus?
static const char *sdStatus = SD_STATE_INIT;

#if HAL_USE_MMC_SPI
// Don't re-read SD card spi device after boot - it could change mid transaction (TS thread could preempt),
// which will cause disaster (usually multiple-unlock of the same mutex in UNLOCK_SD_SPI)
static spi_device_e mmcSpiDevice = SPI_NONE;

// MMC/SD driver instance.
MMCDriver MMCD1;

// MMC/SD over SPI driver configuration
static MMCConfig mmccfg = { NULL, &mmc_ls_spicfg, &mmc_hs_spicfg };

#define LOCK_SD_SPI lockSpi(mmcSpiDevice)
#define UNLOCK_SD_SPI unlockSpi(mmcSpiDevice)

#endif /* HAL_USE_MMC_SPI */

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

static FATFS& MMC_FS = mmcCardCacheControlledStorage.usedPart.fs;

namespace sd_mem {
FIL* getLogFileFd() {
	return &mmcCardCacheControlledStorage.usedPart.file;
}

SdLogBufferWriter& getLogBuffer() {
	return mmcCardCacheControlledStorage.usedPart.logBuffer;
}
} // namespace sd_mem

#if HAL_USE_USB_MSD

static chibios_rt::BinarySemaphore usbConnectedSemaphore(/* taken =*/ true);

void onUsbConnectedNotifyMmcI() {
	usbConnectedSemaphore.signalI();
}

#endif /* HAL_USE_USB_MSD */

#if HAL_USE_MMC_SPI
/*
 * Attempts to initialize the MMC card.
 * Returns a BaseBlockDevice* corresponding to the SD card if successful, otherwise nullptr.
 */
static BaseBlockDevice* initializeMmcBlockDevice() {
	// Don't try to mount SD card in case of fatal error - hardware may be in an unexpected state
	if (hasFirmwareError()) {
		return nullptr;
	}
	
	if (!engineConfiguration->isSdCardEnabled || engineConfiguration->sdCardSpiDevice == SPI_NONE) {
		return nullptr;
	}

	// Configures and activates the MMC peripheral.
	mmcSpiDevice = engineConfiguration->sdCardSpiDevice;

	// todo: reuse initSpiCs method?
	mmc_hs_spicfg.ssport = mmc_ls_spicfg.ssport = getHwPort("mmc", engineConfiguration->sdCardCsPin);
	mmc_hs_spicfg.sspad = mmc_ls_spicfg.sspad = getHwPin("mmc", engineConfiguration->sdCardCsPin);
	mmccfg.spip = getSpiDevice(mmcSpiDevice);

	// Invalid SPI device, abort.
	if (!mmccfg.spip) {
		return nullptr;
	}

	// We think we have everything for the card, let's try to mount it!
	mmcObjectInit(&MMCD1);
	mmcStart(&MMCD1, &mmccfg);

	// Performs the initialization procedure on the inserted card.
	LOCK_SD_SPI;
	sdStatus = SD_STATE_CONNECTING;
	if (mmcConnect(&MMCD1) != HAL_SUCCESS) {
		sdStatus = SD_STATE_MMC_FAILED;
		UNLOCK_SD_SPI;
		return nullptr;
	}
	// We intentionally never unlock in case of success, we take exclusive access of that spi device for SD use

	return reinterpret_cast<BaseBlockDevice*>(&MMCD1);
}
#endif /* HAL_USE_MMC_SPI */

// Some ECUs are wired for SDIO/SDMMC instead of SPI
#ifdef EFI_SDC_DEVICE
static const SDCConfig sdcConfig = {
	SDC_MODE_4BIT
};

static BaseBlockDevice* initializeMmcBlockDevice() {
	if (!engineConfiguration->isSdCardEnabled) {
		return nullptr;
	}

	sdcStart(&EFI_SDC_DEVICE, &sdcConfig);
	sdStatus = SD_STATE_CONNECTING;
	if (sdcConnect(&EFI_SDC_DEVICE) != HAL_SUCCESS) {
		sdStatus = SD_STATE_NOT_CONNECTED;
		return nullptr;
	}

	// STM32H7 SDMMC1 needs the filesystem object to be in AXI
	// SRAM, but excluded from the cache
	#ifdef STM32H7XX
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
#endif /* EFI_SDC_DEVICE */

bool mountSdFilesystem() {
	auto cardBlockDevice = initializeMmcBlockDevice();

#if EFI_TUNER_STUDIO
	// If not null, card is present
	engine->outputChannels.sd_present = cardBlockDevice != nullptr;
#endif

	// if no card, don't try to mount FS
	if (!cardBlockDevice) {
		return false;
	}

#if HAL_USE_USB_MSD
	// Wait for the USB stack to wake up, or a 5 second timeout, whichever occurs first
	msg_t usbResult = usbConnectedSemaphore.wait(TIME_MS2I(5000));

	bool hasUsb = usbResult == MSG_OK;

	// If we have a device AND USB is connected, mount the card to USB, otherwise
	// mount the null device and try to mount the filesystem ourselves
	if (cardBlockDevice && hasUsb) {
		// Mount the real card to USB
		attachMsdSdCard(cardBlockDevice);

		sdStatus = SD_STATE_MSD;
		// At this point we're done: don't try to write files ourselves
		return false;
	}
#endif

	// We were able to connect the SD card, mount the filesystem
	if (f_mount(&MMC_FS, "/", 1) == FR_OK) {
		sdStatus = SD_STATE_MOUNTED;
		efiPrintf("SD card mounted!");
		fs_ready = true;
		return true;
	} else {
		sdStatus = SD_STATE_MOUNT_FAILED;
		return false;
	}
}

void unmountSdFilesystem() {
	if (!fs_ready) {
		efiPrintf("Error: No File system is mounted. \"mountsd\" first");
		return;
	}

	fs_ready = false;

	// Unmount the volume
	f_mount(nullptr, nullptr, 0);						// FatFs: Unregister work area prior to discard it

#if HAL_USE_MMC_SPI
	mmcDisconnect(&MMCD1);						// Brings the driver in a state safe for card removal.
	mmcStop(&MMCD1);							// Disables the MMC peripheral.
	UNLOCK_SD_SPI;
#endif
#ifdef EFI_SDC_DEVICE
	sdcDisconnect(&EFI_SDC_DEVICE);
	sdcStop(&EFI_SDC_DEVICE);
#endif

	efiPrintf("MMC/SD card removed");
}

#endif // EFI_FILE_LOGGING && EFI_PROD_CODE
