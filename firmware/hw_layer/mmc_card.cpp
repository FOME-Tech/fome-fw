/**
 * @file	mmc_card.cpp
 *
 * @date Dec 28, 2013
 * @author Kot_dnz
 * @author Andrey Belomutskiy, (c) 2012-2020
 *
 * default pinouts in case of SPI2 connected to MMC: PB13 - SCK, PB14 - MISO, PB15 - MOSI, PD4 - CS, 3.3v
 * default pinouts in case of SPI3 connected to MMC: PB3  - SCK, PB4  - MISO, PB5  - MOSI, PD4 - CS, 3.3v
 *
 *
 * todo: extract some logic into a controller file
 */

#include "pch.h"

#if EFI_FILE_LOGGING

#include "buffered_writer.h"
#include "status_loop.h"
#include "binary_logging.h"

static bool fs_ready = false;

int totalLoggedBytes = 0;

#if EFI_PROD_CODE

static int fileCreatedCounter = 0;
static int writeCounter = 0;
static int totalWritesCounter = 0;
static int totalSyncCounter = 0;

#include <stdio.h>
#include <string.h>
#include "mmc_card.h"
#include "ff.h"
#include "mass_storage_init.h"

#include "rtc_helper.h"

#include <charconv>

#define SD_STATE_INIT "init"
#define SD_STATE_MOUNTED "MOUNTED"
#define SD_STATE_MOUNT_FAILED "MOUNT_FAILED"
#define SD_STATE_OPEN_FAILED "OPEN_FAILED"
#define SD_STATE_NOT_INSERTED "NOT_INSERTED"
#define SD_STATE_CONNECTING "CONNECTING"
#define SD_STATE_MSD "MSD"
#define SD_STATE_NOT_CONNECTED "NOT_CONNECTED"
#define SD_STATE_MMC_FAILED "MMC_CONNECT_FAILED"

// todo: shall we migrate to enum with enum2string for consistency? maybe not until we start reading sdStatus?
static const char *sdStatus = SD_STATE_INIT;

// at about 20Hz we write about 2Kb per second, looks like we flush once every ~2 seconds
#define F_SYNC_FREQUENCY 10

/**
 * on't re-read SD card spi device after boot - it could change mid transaction (TS thread could preempt),
 * which will cause disaster (usually multiple-unlock of the same mutex in UNLOCK_SD_SPI)
 */
static spi_device_e mmcSpiDevice = SPI_NONE;

#define LOG_INDEX_FILENAME "index.txt"

#define FOME_LOG_PREFIX "fome_"
#define PREFIX_LEN 5
#define SHORT_TIME_LEN 13

#define LS_RESPONSE "ls_result"
#define FILE_LIST_MAX_COUNT 20

#if HAL_USE_MMC_SPI
/**
 * MMC driver instance.
 */
MMCDriver MMCD1;

/* MMC/SD over SPI driver configuration.*/
static MMCConfig mmccfg = { NULL, &mmc_ls_spicfg, &mmc_hs_spicfg };

#define LOCK_SD_SPI lockSpi(mmcSpiDevice)
#define UNLOCK_SD_SPI unlockSpi(mmcSpiDevice)

#endif /* HAL_USE_MMC_SPI */

struct SdLogBufferWriter final : public BufferedWriter<512> {
	bool failed = false;

	size_t writeInternal(const char* buffer, size_t count) override;
};

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
static FIL& FDLogFile = mmcCardCacheControlledStorage.usedPart.file;
static SdLogBufferWriter& logBuffer = mmcCardCacheControlledStorage.usedPart.logBuffer;

static void mmcUnMount();

static void setSdCardReady(bool value) {
	fs_ready = value;
}

// print FAT error function
static void printError(const char *str, FRESULT f_error) {
	efiPrintf("FatFs Error \"%s\" %d", str, f_error);
}

// 10 because we want at least 4 character name
#define MIN_FILE_INDEX 10
static char logName[_MAX_FILLER + 20];

static void sdStatistics() {
	efiPrintf("SD enabled=%s status=%s", boolToString(engineConfiguration->isSdCardEnabled),
			sdStatus);
	printSpiConfig("SD", mmcSpiDevice);
	if (isSdCardAlive()) {
		efiPrintf("filename=%s size=%d", logName, totalLoggedBytes);
	}
}

static int incLogFileName() {
	// tl;dr: figure out the name of the next log file
	// 1. open the index file, read/parse the current counter
	// 2. increment it
	// 3. write back to the index file
	int logFileIndex = MIN_FILE_INDEX;

	FRESULT err = f_open(&FDLogFile, LOG_INDEX_FILENAME, FA_READ);
	if (err != FR_OK && err != FR_EXIST) {
		efiPrintf("SD log index file (%s) not found or error: %d", LOG_INDEX_FILENAME, err);
		goto err;
	}

	char data[20];
	UINT fileLength;

	err = f_read(&FDLogFile, (void*)data, sizeof(data), &fileLength);
	if (err != FR_OK) {
		efiPrintf("SD log index file (%s) failed to read: %d", LOG_INDEX_FILENAME, err);
		goto err;
	}

	if (std::errc{} == std::from_chars(data, data + fileLength, logFileIndex).ec) {
		efiPrintf("SD log index (%s) size %d parsed index %d", data, fileLength, logFileIndex);
		logFileIndex++;
	} else {
		// Parse failure, reset to first file
		logFileIndex = MIN_FILE_INDEX;
	}

err:
	// Even in case of error, attempt to write the current index back to the
	// file so we can read it out next time (and not fail)
	f_close(&FDLogFile);
	err = f_open(&FDLogFile, LOG_INDEX_FILENAME, FA_OPEN_ALWAYS | FA_WRITE);
	itoa10(data, logFileIndex);
	f_write(&FDLogFile, (void*)data, strlen(data), nullptr);
	f_close(&FDLogFile);
	efiPrintf("Done %d", logFileIndex);

	return logFileIndex;
}

static void prepareLogFileName(int index) {
	strcpy(logName, FOME_LOG_PREFIX);
	char *ptr;

	if (dateToStringShort(&logName[PREFIX_LEN])) {
		ptr = &logName[PREFIX_LEN + SHORT_TIME_LEN];
	} else {
		ptr = itoa10(&logName[PREFIX_LEN], index);
	}

	if (engineConfiguration->sdTriggerLog) {
		strcat(ptr, ".teeth");
	} else {
		strcat(ptr, DOT_MLG);
	}
}

/**
 * @brief Create a new file with the specified name
 *
 * This function saves the name of the file in a global variable
 * so that we can later append to that file
 */
static void createLogFile(int logFileIndex) {
	prepareLogFileName(logFileIndex);

	FRESULT err = f_open(&FDLogFile, logName, FA_CREATE_ALWAYS | FA_WRITE);				// Create new file
	if (err != FR_OK && err != FR_EXIST) {
		sdStatus = SD_STATE_OPEN_FAILED;
		warning(ObdCode::CUSTOM_ERR_SD_MOUNT_FAILED, "SD: mount failed");
		printError("FS mount failed", err);	// else - show error
		return;
	}

	setSdCardReady(true);						// everything Ok
}

/*
 * MMC card un-mount.
 */
static void mmcUnMount() {
	if (!isSdCardAlive()) {
		efiPrintf("Error: No File system is mounted. \"mountsd\" first");
		return;
	}

	setSdCardReady(false);

	// Close the log file (ignore errors, we're already in the shutdown path)
	f_close(&FDLogFile);

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

// Initialize and mount the SD card.
// Returns true if the filesystem was successfully mounted for writing.
static bool mountMmc() {
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
		int logFileIndex = incLogFileName();
		createLogFile(logFileIndex);
		fileCreatedCounter++;
		efiPrintf("MMC/SD mounted!");
		return true;
	} else {
		sdStatus = SD_STATE_MOUNT_FAILED;
		return false;
	}
}

size_t SdLogBufferWriter::writeInternal(const char* buffer, size_t count) {
	size_t bytesWritten;

	totalLoggedBytes += count;

	FRESULT err = f_write(&FDLogFile, buffer, count, &bytesWritten);

	if (bytesWritten != count) {
		printError("write error or disk full", err);

		// Close file and unmount volume
		mmcUnMount();
		failed = true;
		return 0;
	} else {
		writeCounter++;
		totalWritesCounter++;
		if (writeCounter >= F_SYNC_FREQUENCY) {
			/**
			 * Performance optimization: not f_sync after each line, f_sync is probably a heavy operation
			 * todo: one day someone should actually measure the relative cost of f_sync
			 */
			f_sync(&FDLogFile);
			totalSyncCounter++;
			writeCounter = 0;
		}
	}

	return bytesWritten;
}

#else // not EFI_PROD_CODE (simulator)

#include <fstream>

bool mountMmc() {
	// Stub so the loop thinks the MMC mounted OK
	return true;
}

class SdLogBufferWriter final : public BufferedWriter<512> {
public:
	bool failed = false;

	SdLogBufferWriter()
		: m_stream("fome_simulator_log.mlg", std::ios::binary | std::ios::trunc)
	{
		fs_ready = true;
	}

	size_t writeInternal(const char* buffer, size_t count) override {
		m_stream.write(buffer, count);
		m_stream.flush();
		return count;
	}

private:
	std::ofstream m_stream;
};

static SdLogBufferWriter logBuffer;

#endif // EFI_PROD_CODE

// Log 'regular' ECU log to MLG file
static void mlgLogger();

// Log binary trigger log
static void sdTriggerLogger();

static THD_WORKING_AREA(mmcThreadStack, 3 * UTILITY_THREAD_STACK_SIZE);		// MMC monitor thread
static THD_FUNCTION(MMCmonThread, arg) {
	(void)arg;
	chRegSetThreadName("MMC Card Logger");

	if (!mountMmc()) {
		// no card present (or mounted via USB), don't do internal logging
		return;
	}

	#if EFI_TUNER_STUDIO
		engine->outputChannels.sd_logging_internal = true;
	#endif

	if (engineConfiguration->sdTriggerLog) {
		sdTriggerLogger();
	} else {
		mlgLogger();
	}
}

void mlgLogger() {
	while (true) {
		// if the SPI device got un-picked somehow, cancel SD card
		// Don't do this check at all if using SDMMC interface instead of SPI
#if EFI_PROD_CODE && !defined(EFI_SDC_DEVICE)
		if (engineConfiguration->sdCardSpiDevice == SPI_NONE) {
			return;
		}
#endif

		systime_t before = chVTGetSystemTime();

		writeSdLogLine(logBuffer);

		// Something went wrong (already handled), so cancel further writes
		if (logBuffer.failed) {
			return;
		}

		auto freq = engineConfiguration->sdCardLogFrequency;
		if (freq > 250) {
			freq = 250;
		} else if (freq < 1) {
			freq = 1;
		}

		systime_t period = CH_CFG_ST_FREQUENCY / freq;
		chThdSleepUntilWindowed(before, before + period);
	}
}

static void sdTriggerLogger() {
#if EFI_TOOTH_LOGGER
	EnableToothLogger();

	while (true) {
		auto buffer = GetToothLoggerBufferBlocking();

		if (buffer) {
			logBuffer.write(reinterpret_cast<const char*>(buffer->buffer), buffer->nextIdx * sizeof(composite_logger_s));
			ReturnToothLoggerBuffer(buffer);
		}

	}
#endif /* EFI_TOOTH_LOGGER */
}

bool isSdCardAlive(void) {
	return fs_ready;
}

// Pre-config load init
void initEarlyMmcCard() {
#if EFI_PROD_CODE
	logName[0] = 0;

	addConsoleAction("sdinfo", sdStatistics);
#endif // EFI_PROD_CODE
}

void initMmcCard() {
	chThdCreateStatic(mmcThreadStack, sizeof(mmcThreadStack), PRIO_MMC, (tfunc_t)(void*) MMCmonThread, NULL);
}

#endif /* EFI_FILE_LOGGING */
