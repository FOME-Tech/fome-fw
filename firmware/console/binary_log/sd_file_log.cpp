#include "pch.h"

#if EFI_FILE_LOGGING

#include "mmc_card.h"
#include "binary_logging.h"

int totalLoggedBytes = 0;

#if EFI_PROD_CODE

static int writeCounter = 0;
static int totalWritesCounter = 0;
static int totalSyncCounter = 0;

#include <stdio.h>
#include <string.h>
#include "ff.h"
#include "mass_storage_init.h"
#include "rtc_helper.h"
#include <charconv>

// 10 because we want at least 4 character name
#define MIN_FILE_INDEX 10
static char logName[_MAX_FILLER + 20];

// at about 20Hz we write about 2Kb per second, looks like we flush once every ~2 seconds
#define F_SYNC_FREQUENCY 10

#define LOG_INDEX_FILENAME "index.txt"

#define FOME_LOG_PREFIX "fome_"
#define PREFIX_LEN 5
#define SHORT_TIME_LEN 13

// print FAT error function
static void printFatFsError(const char *str, FRESULT err) {
	efiPrintf("FatFs Error \"%s\" %d", str, err);
}

static int incLogFileName() {
	// tl;dr: figure out the name of the next log file
	// 1. open the index file, read/parse the current counter
	// 2. increment it
	// 3. write back to the index file
	int logFileIndex = MIN_FILE_INDEX;

	memset(sd_mem::getLogFileFd(), 0, sizeof(FIL));
	FRESULT err = f_open(sd_mem::getLogFileFd(), LOG_INDEX_FILENAME, FA_READ);
	if (err != FR_OK && err != FR_EXIST) {
		efiPrintf("SD log index file (%s) not found or error: %d", LOG_INDEX_FILENAME, err);
		goto err;
	}

	char data[20];
	UINT fileLength;

	err = f_read(sd_mem::getLogFileFd(), (void*)data, sizeof(data), &fileLength);
	if (err != FR_OK) {
		efiPrintf("SD log index file (%s) failed to read: %d", LOG_INDEX_FILENAME, err);
		goto err;
	}

	if (fileLength == 0) {
		// File exists but no bytes read?
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
	f_close(sd_mem::getLogFileFd());

	memset(sd_mem::getLogFileFd(), 0, sizeof(FIL));
	err = f_open(sd_mem::getLogFileFd(), LOG_INDEX_FILENAME, FA_OPEN_ALWAYS | FA_WRITE);
	itoa10(data, logFileIndex);
	f_write(sd_mem::getLogFileFd(), (void*)data, strlen(data), nullptr);
	f_close(sd_mem::getLogFileFd());
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
		strcat(ptr, ".mlg");
	}
}

/**
 * @brief Create a new file with the specified name
 *
 * This function saves the name of the file in a global variable
 * so that we can later append to that file
 */
static bool createLogFile(int logFileIndex) {
	prepareLogFileName(logFileIndex);

	memset(sd_mem::getLogFileFd(), 0, sizeof(FIL));
	FRESULT err = f_open(sd_mem::getLogFileFd(), logName, FA_CREATE_ALWAYS | FA_WRITE);
	if (err != FR_OK && err != FR_EXIST) {
		warning(ObdCode::CUSTOM_ERR_SD_MOUNT_FAILED, "SD: mount failed");
		printFatFsError("FS mount failed", err);	// else - show error
		return false;
	}

	return true;
}

size_t SdLogBufferWriter::writeInternal(const char* buffer, size_t count) {
	size_t bytesWritten;

	totalLoggedBytes += count;

	FRESULT err = f_write(sd_mem::getLogFileFd(), buffer, count, &bytesWritten);

	if (bytesWritten != count) {
		printFatFsError("write error or disk full", err);

		// Close file and unmount volume (ignore errors, we're already in the shutdown path)
		f_close(sd_mem::getLogFileFd());

		unmountSdFilesystem();
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
			f_sync(sd_mem::getLogFileFd());
			totalSyncCounter++;
			writeCounter = 0;
		}
	}

	return bytesWritten;
}

#else // not EFI_PROD_CODE (simulator)

#include <fstream>

bool mountSdFilesystem() {
	// Stub so the loop thinks the MMC mounted OK
	return true;
}

class SdLogBufferWriter final : public BufferedWriter<512> {
public:
	bool failed = false;

	SdLogBufferWriter()
		: m_stream("fome_simulator_log.mlg", std::ios::binary | std::ios::trunc)
	{
	}

	size_t writeInternal(const char* buffer, size_t count) override {
		m_stream.write(buffer, count);
		m_stream.flush();
		return count;
	}

private:
	std::ofstream m_stream;
};

namespace sd_mem {
	SdLogBufferWriter& getLogBuffer() {
		static SdLogBufferWriter logBuffer;
		return logBuffer;
	}
}

#endif // EFI_PROD_CODE

// Log 'regular' ECU log to MLG file
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

		writeSdLogLine(sd_mem::getLogBuffer());

		// Something went wrong (already handled), so cancel further writes
		if (sd_mem::getLogBuffer().failed) {
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

// Log binary trigger log
static void sdTriggerLogger() {
#if EFI_TOOTH_LOGGER
	EnableToothLogger();

	while (true) {
		auto buffer = GetToothLoggerBufferBlocking();

		if (buffer) {
			sd_mem::getLogBuffer().write(reinterpret_cast<const char*>(buffer->buffer), buffer->nextIdx * sizeof(composite_logger_s));
			ReturnToothLoggerBuffer(buffer);
		}

	}
#endif /* EFI_TOOTH_LOGGER */
}

static THD_WORKING_AREA(sdCardLoggerStack, 3 * UTILITY_THREAD_STACK_SIZE);		// MMC monitor thread
static THD_FUNCTION(sdCardLoggerThread, arg) {
	(void)arg;
	chRegSetThreadName("MMC Card Logger");

	if (!mountSdFilesystem()) {
		// no card present (or mounted via USB), don't do internal logging
		return;
	}

	#if EFI_PROD_CODE
		int logFileIndex = incLogFileName();
		if (!createLogFile(logFileIndex)) {
			return;
		}
	#endif // EFI_PROD_CODE

	#if EFI_TUNER_STUDIO
		engine->outputChannels.sd_logging_internal = true;
	#endif

	if (engineConfiguration->sdTriggerLog) {
		sdTriggerLogger();
	} else {
		mlgLogger();
	}
}

void initSdCardLogger() {
	chThdCreateStatic(sdCardLoggerStack, sizeof(sdCardLoggerStack), SD_CARD_LOGGER, sdCardLoggerThread, nullptr);
}

#endif // EFI_FILE_LOGGING
