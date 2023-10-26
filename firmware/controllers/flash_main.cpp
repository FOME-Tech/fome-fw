/**
 * @file    flash_main.cpp
 * @brief	Higher-level logic of saving data into internal flash memory
 *
 *
 * @date Sep 19, 2013
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"

#if EFI_INTERNAL_FLASH

#include "mpu_util.h"
#include "flash_main.h"
#include "eficonsole.h"

#include "flash_int.h"

#if EFI_TUNER_STUDIO
#include "tunerstudio.h"
#endif

#if EFI_STORAGE_EXT_SNOR == TRUE
#include "hal_serial_nor.h"
#include "hal_mfs.h"
#endif

#include "runtime_state.h"

static bool needToWriteConfiguration = false;

/* if we store settings externally */
#if EFI_STORAGE_EXT_SNOR == TRUE

/* Some fields in following struct is used for DMA transfers, so do no cache */
NO_CACHE SNORDriver snor1;

const WSPIConfig WSPIcfg1 = {
	.end_cb			= NULL,
	.error_cb		= NULL,
	.dcr			= STM32_DCR_FSIZE(23U) |	/* 8MB device.          */
					  STM32_DCR_CSHT(1U)		/* NCS 2 cycles delay.  */
};

const SNORConfig snorcfg1 = {
	.busp			= &WSPID1,
	.buscfg			= &WSPIcfg1
};

/* Managed Flash Storage stuff */
MFSDriver mfsd;

const MFSConfig mfsd_nor_config = {
	.flashp			= (BaseFlash *)&snor1,
	.erased			= 0xFFFFFFFFU,
	.bank_size		= 64 * 1024U,
	.bank0_start	= 0U,
	.bank0_sectors	= 128U,	/* 128 * 4 K = 0.5 Mb */
	.bank1_start	= 128U,
	.bank1_sectors	= 128U
};

#define EFI_MFS_SETTINGS_RECORD_ID		1

#endif

/**
 * https://sourceforge.net/p/rusefi/tickets/335/
 *
 * In order to preserve at least one copy of the tune in case of electrical issues address of second configuration copy
 * should be in a different sector of flash since complete flash sectors are erased on write.
 */

static uint32_t flashStateCrc(const persistent_config_container_s& state) {
	return crc32(&state.persistentConfiguration, sizeof(persistent_config_s));
}

#if EFI_FLASH_WRITE_THREAD
chibios_rt::BinarySemaphore flashWriteSemaphore(/*taken =*/ true);

#if EFI_STORAGE_EXT_SNOR == TRUE
/* in case of MFS we need more stack */
static THD_WORKING_AREA(flashWriteStack, 3 * UTILITY_THREAD_STACK_SIZE);
#else
static THD_WORKING_AREA(flashWriteStack, UTILITY_THREAD_STACK_SIZE);
#endif
static void flashWriteThread(void*) {
	chRegSetThreadName("flash writer");

	while (true) {
		// Wait for a request to come in
		flashWriteSemaphore.wait();

		// Do the actual flash write operation
		writeToFlashNow();
	}
}
#endif // EFI_FLASH_WRITE_THREAD

void setNeedToWriteConfiguration() {
	efiPrintf("Scheduling configuration write");
	needToWriteConfiguration = true;

#if EFI_FLASH_WRITE_THREAD
	if (allowFlashWhileRunning() || (EFI_STORAGE_EXT_SNOR == TRUE)) {
		// Signal the flash writer thread to wake up and write at its leisure
		flashWriteSemaphore.signal();
	}
#endif // EFI_FLASH_WRITE_THREAD
}

bool getNeedToWriteConfiguration() {
	return needToWriteConfiguration;
}

void writeToFlashIfPending() {
	// with a flash write thread, the schedule happens directly from
	// setNeedToWriteConfiguration, so there's nothing to do here
	if (allowFlashWhileRunning() || !getNeedToWriteConfiguration()) {
		// Allow sensor timeouts again now that we're done (and a little time has passed)
		Sensor::inhibitTimeouts(false);
		return;
	}

	// Prevent sensor timeouts while flashing
	Sensor::inhibitTimeouts(true);
	writeToFlashNow();
	// we do not want to allow sensor timeouts right away, we re-enable next time method is invoked
}

// Erase and write a copy of the configuration at the specified address
template <typename TStorage>
int eraseAndFlashCopy(flashaddr_t storageAddress, const TStorage& data) {
	// error already reported, return
	if (!storageAddress) {
		return FLASH_RETURN_SUCCESS;
	}

	auto err = intFlashErase(storageAddress, sizeof(TStorage));
	if (FLASH_RETURN_SUCCESS != err) {
		firmwareError(ObdCode::OBD_PCM_Processor_Fault, "Failed to erase flash at 0x%08x: %d", storageAddress, err);
		return err;
	}

	err = intFlashWrite(storageAddress, reinterpret_cast<const char*>(&data), sizeof(TStorage));
	if (FLASH_RETURN_SUCCESS != err) {
		firmwareError(ObdCode::OBD_PCM_Processor_Fault, "Failed to write flash at 0x%08x: %d", storageAddress, err);
		return err;
	}

	return err;
}

bool burnWithoutFlash = false;

void writeToFlashNow() {
	engine->configBurnTimer.reset();
	bool isSuccess = false;

	if (burnWithoutFlash) {
		needToWriteConfiguration = false;
		return;
	}
	efiPrintf("Writing pending configuration...");

	// Set up the container
	persistentState.size = sizeof(persistentState);
	persistentState.version = FLASH_DATA_VERSION;
	persistentState.value = flashStateCrc(persistentState);

#if EFI_STORAGE_EXT_SNOR == TRUE
	mfs_error_t err;
	/* In case of MFS:
	 * do we need to have two copies?
	 * do we need to protect it with CRC? */

	err = mfsWriteRecord(&mfsd, EFI_MFS_SETTINGS_RECORD_ID,
						 sizeof(persistentState), (uint8_t *)&persistentState);

	if (err == MFS_NO_ERROR)
		isSuccess = true;
#endif

#if EFI_STORAGE_INT_FLASH == TRUE
	// Flash two copies
	int result1 = eraseAndFlashCopy(getFlashAddrFirstCopy(), persistentState);
	int result2 = FLASH_RETURN_SUCCESS;
	/* Only if second copy is supported */
	if (getFlashAddrSecondCopy()) {
		result2 = eraseAndFlashCopy(getFlashAddrSecondCopy(), persistentState);
	}

	// handle success/failure
	isSuccess = (result1 == FLASH_RETURN_SUCCESS) && (result2 == FLASH_RETURN_SUCCESS);
#endif

	if (isSuccess) {
		efiPrintf("FLASH_SUCCESS");
	} else {
		efiPrintf("Flashing failed");
	}

	resetMaxValues();

	// Write complete, clear the flag
	needToWriteConfiguration = false;
}

static void doResetConfiguration() {
	resetConfigurationExt(engineConfiguration->engineType);
}

enum class FlashState {
	Ok,
	CrcFailed,
	IncompatibleVersion,
	// all is well, but we're on a fresh chip with blank memory
	BlankChip,
};

/**
 * Read single copy of rusEFI configuration from flash
 */
static FlashState readOneConfigurationCopy(flashaddr_t address) {
	efiPrintf("readFromFlash %x", address);

	// error already reported, return
	if (!address) {
		return FlashState::BlankChip;
	}

	intFlashRead(address, (char *) &persistentState, sizeof(persistentState));

	auto flashCrc = flashStateCrc(persistentState);

	if (flashCrc != persistentState.value) {
		// If the stored crc is all 1s, that probably means the flash is actually blank, not that the crc failed.
		if (persistentState.value == ((decltype(persistentState.value))-1)) {
			return FlashState::BlankChip;
		} else {
			return FlashState::CrcFailed;
		}
	} else if (persistentState.version != FLASH_DATA_VERSION || persistentState.size != sizeof(persistentState)) {
		return FlashState::IncompatibleVersion;
	} else {
		return FlashState::Ok;
	}
}

/**
 * this method could and should be executed before we have any
 * connectivity so no console output here
 *
 * in this method we read first copy of configuration in flash. if that first copy has CRC or other issues we read second copy.
 */
static FlashState readConfiguration() {
#if EFI_STORAGE_EXT_SNOR == TRUE
	size_t settings_size = sizeof(persistentState);
	mfs_error_t err = mfsReadRecord(&mfsd, EFI_MFS_SETTINGS_RECORD_ID,
						&settings_size, (uint8_t *)&persistentState);

	// TODO: check err result better?
	if (err == MFS_NO_ERROR) {
		return FlashState::Ok;
	} else {
		// TODO: is this correct?
		return FlashState::BlankChip;
	}
#endif

#if EFI_STORAGE_INT_FLASH == TRUE
	auto firstCopyAddr = getFlashAddrFirstCopy();
	auto secondyCopyAddr = getFlashAddrSecondCopy();

	FlashState firstCopy = readOneConfigurationCopy(firstCopyAddr);

	if (firstCopy == FlashState::Ok) {
		// First copy looks OK, don't even need to check second copy.
		return firstCopy;
	}

	/* no second copy? */
	if (getFlashAddrSecondCopy() == 0x0) {
		return firstCopy;
	}

	efiPrintf("Reading second configuration copy");
	return readOneConfigurationCopy(secondyCopyAddr);
#endif

	// In case of neither of those cases, return that things went OK?
	return FlashState::Ok;
}

void readFromFlash() {
	FlashState result = readConfiguration();

	switch (result) {
		case FlashState::CrcFailed:
			warning(ObdCode::CUSTOM_ERR_FLASH_CRC_FAILED, "flash CRC failed");
			efiPrintf("Need to reset flash to default due to CRC mismatch");
			[[fallthrough]];
		case FlashState::BlankChip:
			resetConfigurationExt(engine_type_e::DEFAULT_ENGINE_TYPE);
			break;
		case FlashState::IncompatibleVersion:
			// Preserve engine type from old config
			efiPrintf("Resetting due to version mismatch but preserving engine type [%d]", engineConfiguration->engineType);
			resetConfigurationExt(engineConfiguration->engineType);
			break;
		case FlashState::Ok:
			// At this point we know that CRC and version number is what we expect. Safe to assume it's a valid configuration.
			applyNonPersistentConfiguration();
			efiPrintf("Read valid configuration from flash!");
			break;
	}

	// we can only change the state after the CRC check
	engineConfiguration->byFirmwareVersion = getRusEfiVersion();
	validateConfiguration();
}

static void rewriteConfig() {
	doResetConfiguration();
	writeToFlashNow();
}

void initFlash() {
#if EFI_STORAGE_EXT_SNOR == TRUE
	mfs_error_t err;

#if SNOR_SHARED_BUS == FALSE
	wspiStart(&WSPID1, &WSPIcfg1);
#endif

	/* Initializing and starting snor1 driver.*/
	snorObjectInit(&snor1);
	snorStart(&snor1, &snorcfg1);

	/* MFS */
	mfsObjectInit(&mfsd);
	err = mfsStart(&mfsd, &mfsd_nor_config);
	if (err != MFS_NO_ERROR) {
		/* hm...? */
	}
#endif

	addConsoleAction("readconfig", readFromFlash);
	/**
	 * This would write NOW (you should not be doing this while connected to real engine)
	 */
	addConsoleAction(CMD_WRITECONFIG, writeToFlashNow);
#if EFI_TUNER_STUDIO
	/**
	 * This would schedule write to flash once the engine is stopped
	 */
	addConsoleAction(CMD_BURNCONFIG, requestBurn);
#endif
	addConsoleAction("resetconfig", doResetConfiguration);
	addConsoleAction("rewriteconfig", rewriteConfig);

#if EFI_FLASH_WRITE_THREAD
	if (allowFlashWhileRunning()) {
		chThdCreateStatic(flashWriteStack, sizeof(flashWriteStack), PRIO_FLASH_WRITE, flashWriteThread, nullptr);
	}
#endif
}

#endif /* EFI_INTERNAL_FLASH */
