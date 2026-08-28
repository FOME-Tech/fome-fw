#include "pch.h"

#if EFI_FILE_LOGGING

#include "mmc_card.h"
#include "dma_buffers.h"
#include "binary_logging.h"

int totalLoggedBytes = 0;

static int getSdLogFrequency() {
	int freq = engineConfiguration->sdCardLogFrequency;

	if (freq > 100) {
		freq = 100;
	} else if (freq < 1) {
		freq = 1;
	}

	return freq;
}

#if EFI_PROD_CODE

static int totalWritesCounter = 0;
static int totalSyncCounter = 0;

#include <stdio.h>
#include <string.h>
#include "efi_timer.h"
#include "ff.h"
#include "mass_storage_init.h"
#include "rtc_helper.h"
#include <charconv>

// 10 because we want at least 4 character name
#define MIN_FILE_INDEX 10
static char logName[_MAX_FILLER + 20];

// This is the window of log data lost on a power cut: the bytes are already on the card, but
// the directory entry that gives the file its length is only up to date as of the last sync.
#define F_SYNC_PERIOD_SEC 2

static Timer syncTimer;

#define LOG_INDEX_FILENAME "index.txt"

#define FOME_LOG_PREFIX "fome_"
#define PREFIX_LEN 5
#define SHORT_TIME_LEN 13

// print FAT error function
static void printFatFsError(const char* str, FRESULT err) {
	efiPrintf("FatFs Error \"%s\" %d", str, err);
}

static int incLogFileName() {
	// tl;dr: figure out the name of the next log file
	// 1. open the index file, read/parse the current counter
	// 2. increment it
	// 3. write back to the index file
	int logFileIndex = MIN_FILE_INDEX;

	memset(dma_buffers::logFileFd(), 0, sizeof(FIL));
	FRESULT err = f_open(dma_buffers::logFileFd(), LOG_INDEX_FILENAME, FA_READ);
	if (err != FR_OK && err != FR_EXIST) {
		efiPrintf("SD log index file (%s) not found or error: %d", LOG_INDEX_FILENAME, err);
		goto err;
	}

	char data[20];
	UINT fileLength;

	err = f_read(dma_buffers::logFileFd(), (void*)data, sizeof(data) - 1, &fileLength);
	data[fileLength] = '\0';
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
	f_close(dma_buffers::logFileFd());

	memset(dma_buffers::logFileFd(), 0, sizeof(FIL));
	err = f_open(dma_buffers::logFileFd(), LOG_INDEX_FILENAME, FA_OPEN_ALWAYS | FA_WRITE);
	itoa10(data, logFileIndex);

	UINT bytesWritten;
	f_write(dma_buffers::logFileFd(), (void*)data, strlen(data), &bytesWritten);
	f_close(dma_buffers::logFileFd());
	efiPrintf("Done %d", logFileIndex);

	return logFileIndex;
}

static void prepareLogFileName(int index) {
	strcpy(logName, FOME_LOG_PREFIX);
	char* ptr;

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

	// Print before the open, not after it. f_open() with FA_CREATE_ALWAYS scans the directory and
	// frees any existing cluster chain, so it's a card operation that can block for a long time -
	// if this is the last line in the log, that's where we are.
	efiPrintf("SD: creating log file %s", logName);

	memset(dma_buffers::logFileFd(), 0, sizeof(FIL));
	FRESULT err = f_open(dma_buffers::logFileFd(), logName, FA_CREATE_ALWAYS | FA_WRITE);
	if (err != FR_OK && err != FR_EXIST) {
		warning(ObdCode::CUSTOM_ERR_SD_MOUNT_FAILED, "SD: failed to create log file");
		printFatFsError("failed to create log file", err);
		return false;
	}

	return true;
}

size_t SdLogBufferWriter::writeInternal(const char* buffer, size_t count) {
	size_t bytesWritten;

	totalLoggedBytes += count;

	FRESULT err = f_write(dma_buffers::logFileFd(), buffer, count, &bytesWritten);

	if (bytesWritten != count) {
		// This is terminal: the logger thread exits right after this and never retries, so print
		// everything we know about how we got here. It can't repeat, so it can't spam.
		warning(ObdCode::CUSTOM_ERR_SD_WRITE_FAILED, "SD: logging stopped, write failed");
		printFatFsError("write error or disk full", err);
		efiPrintf(
				"SD: wrote %d of %d bytes at file offset %d",
				(int)bytesWritten,
				(int)count,
				(int)f_tell(dma_buffers::logFileFd()));

		// Close file and unmount volume (ignore errors, we're already in the shutdown path)
		f_close(dma_buffers::logFileFd());

		unmountSdFilesystem();
		failed = true;
		return 0;
	} else {
		totalWritesCounter++;

		if (syncTimer.hasElapsedSec(F_SYNC_PERIOD_SEC)) {
			syncTimer.reset();

			FRESULT syncErr = f_sync(dma_buffers::logFileFd());

			if (syncErr != FR_OK) {
				// Not fatal on its own, but the card is unhappy and the next write is likely to
				// fail outright, so this is the earliest warning we get.
				warning(ObdCode::CUSTOM_ERR_SD_WRITE_FAILED, "SD: f_sync failed %d", syncErr);
			}

			totalSyncCounter++;
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

class SdLogBufferWriter final : public BufferedWriter<SD_LOG_BUFFER_SIZE> {
public:
	bool failed = false;

	SdLogBufferWriter()
		: m_stream("fome_simulator_log.mlg", std::ios::binary | std::ios::trunc) {}

	size_t writeInternal(const char* buffer, size_t count) override {
		m_stream.write(buffer, count);
		m_stream.flush();
		return count;
	}

private:
	std::ofstream m_stream;
};

namespace dma_buffers {
SdLogBufferWriter& logBuffer() {
	static SdLogBufferWriter logBuffer;
	return logBuffer;
}
} // namespace dma_buffers

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

		writeSdLogLine(dma_buffers::logBuffer());

		// Something went wrong (already handled), so cancel further writes
		if (dma_buffers::logBuffer().failed) {
			return;
		}

		systime_t period = CH_CFG_ST_FREQUENCY / getSdLogFrequency();
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
			dma_buffers::logBuffer().write(
					reinterpret_cast<const char*>(buffer->buffer), buffer->nextIdx * sizeof(composite_logger_s));
			ReturnToothLoggerBuffer(buffer);
		}
	}
#endif /* EFI_TOOTH_LOGGER */
}

static THD_WORKING_AREA(sdCardLoggerStack, 3 * UTILITY_THREAD_STACK_SIZE); // MMC monitor thread
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

	if (engineConfiguration->sdTriggerLog) {
		efiPrintf("SD: logging trigger data to %s", logName);
	} else {
		// Record size scales with the output channel count, so a firmware update can raise the
		// demanded data rate without the user changing anything. This is the first number to
		// check when a card that used to keep up no longer does.
		int bytesPerRecord = getSdLogRecordLength() + MLQ_BLOCK_OVERHEAD;
		int freq = getSdLogFrequency();
		efiPrintf(
				"SD: logging to %s, %d fields, %d bytes/record at %d hz = %d bytes/sec",
				logName,
				(int)getSdLogFieldCount(),
				bytesPerRecord,
				freq,
				bytesPerRecord * freq);
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
