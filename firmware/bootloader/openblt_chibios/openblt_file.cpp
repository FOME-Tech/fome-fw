#include "pch.h"

extern "C" {
#include "boot.h"
#include "file.h"
}

#include "mmc_card.h"

static const blt_char firmwareFilename[] = "/fome_update.srec";

static struct {
	FIL handle;		 /**< FatFS handle to the log-file.                 */
	blt_bool canUse; /**< Flag to indicate if the log-file can be used. */
} logfile;

/*
** \brief     Callback that gets called to check whether a firmware update from
**            local file storage should be started. This could for example be when
**            a switch is pressed, when a certain file is found on the local file
**            storage, etc.
** \return    BLT_TRUE if a firmware update is requested, BLT_FALSE otherwise.
**
****************************************************************************************/
extern "C" blt_bool FileIsFirmwareUpdateRequestedHook() {
	// First, try to attach the SD card
	if (!initializeMmcBlockDevice()) {
		// No SD card present, no update to be done
		return BLT_FALSE;
	}

	// needs to be zeroed according to f_stat docs
	FILINFO fileInfoObject;
	memset(&fileInfoObject, 0, sizeof(fileInfoObject));

	/* Current example implementation looks for a predetermined firmware file on the
	 * SD-card. If the SD-card is accessible and the firmware file was found the firmware
	 * update is started. When successfully completed, the firmware file is deleted.
	 * During the firmware update, progress information is written to a file called
	 * bootlog.txt
	 */
	/* check if firmware file is present and SD-card is accessible */
	if (f_stat(firmwareFilename, &fileInfoObject) == FR_OK) {
		/* check if the filesize is valid and that it is not a directory */
		if ((fileInfoObject.fsize > 0) && (!(fileInfoObject.fattrib & AM_DIR))) {
			/* all conditions are met to start a firmware update from local file storage */
			return BLT_TRUE;
		}
	}
	/* still here so no firmware update request is pending */
	return BLT_FALSE;
}

/*
** \brief     Callback to obtain the filename of the firmware file that should be
**            used during the firmware update from the local file storage. This
**            hook function is called at the beginning of the firmware update from
**            local storage sequence.
** \return    valid firmware filename with full path or BLT_NULL.
**
****************************************************************************************/
extern "C" const blt_char* FileGetFirmwareFilenameHook() {
	return firmwareFilename;
}

/*
** \brief     Callback that gets called to inform the application that a firmware
**            update from local storage just started.
** \return    none.
**
****************************************************************************************/
extern "C" void FileFirmwareUpdateStartedHook() {
	/* create/overwrite the logfile */
	logfile.canUse = BLT_FALSE;
	if (f_open(&logfile.handle, "/bootlog.txt", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK) {
		logfile.canUse = BLT_TRUE;
	}
}

extern "C" void FileFirmwareUpdateCompletedHook(void) {
	/* close the log file */
	if (logfile.canUse == BLT_TRUE) {
		f_close(&logfile.handle);
	}

	// now delete the firmware file from the disk since the update was successful
	f_unlink(firmwareFilename);
}

/*
** \brief     Callback that gets called in case an error occurred during a firmware
**            update. Refer to <file.h> for a list of available error codes.
** \return    none.
**
****************************************************************************************/
extern "C" void FileFirmwareUpdateErrorHook(blt_int8u error_code) {
	/* error detected which stops the firmware update, so close the log file */
	if (logfile.canUse == BLT_TRUE) {
		f_close(&logfile.handle);
	}
}

/*
** \brief     Callback that gets called each time new log information becomes
**            available during a firmware update.
** \param     info_string Pointer to a character array with the log entry info.
** \return    none.
**
****************************************************************************************/
extern "C" void FileFirmwareUpdateLogHook(blt_char* info_string) {
	/* write the string to the log file */
	if (logfile.canUse == BLT_TRUE) {
		if (f_puts(info_string, &logfile.handle) < 0) {
			logfile.canUse = BLT_FALSE;
			f_close(&logfile.handle);
		}
	}
}

bool isSdCardEnabled() {
	return true;
}

#if HAL_USE_MMC_SPI
SPIDriver* bootloaderGetSdCardSpiDevice();
Gpio bootloaderGetSdCardCsPin();

SPIDriver* getSdCardSpiDevice() {
	return bootloaderGetSdCardSpiDevice();
}

Gpio getSdCardCsPin() {
	return bootloaderGetSdCardCsPin();
}
#endif // HAL_USE_MMC_SPI
