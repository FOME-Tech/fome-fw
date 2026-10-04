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

#if !EFI_UNIT_TEST
#include "mpu_util.h"
#endif
#include "flash_main.h"
#include "eficonsole.h"

#include "flash_int.h"
#include "crc_accelerator.h"
#include "configuration_write.h"
#include "electronic_throttle.h"

#if EFI_TUNER_STUDIO
#include "tunerstudio.h"
#endif

#include "runtime_state.h"

static ConfigurationWriteState configurationWriteState;
static ConfigurationWriteResult lastWriteResult{ConfigurationWritePhase::Complete, 0, FLASH_RETURN_SUCCESS, 0};

/**
 * https://sourceforge.net/p/rusefi/tickets/335/
 *
 * In order to preserve at least one copy of the tune in case of electrical issues address of second configuration copy
 * should be in a different sector of flash since complete flash sectors are erased on write.
 */

static uint32_t flashStateCrc(const persistent_config_container_s& state) {
	return singleCrc(&state.persistentConfiguration, sizeof(persistent_config_s));
}

static void writeConfiguration(bool requestedOnly);

#if EFI_FLASH_WRITE_THREAD
chibios_rt::BinarySemaphore flashWriteSemaphore(/*taken =*/true);

static THD_WORKING_AREA(flashWriteStack, UTILITY_THREAD_STACK_SIZE);

static void flashWriteThread(void*) {
	chRegSetThreadName("flash writer");

	while (true) {
		// Wait for a request to come in
		flashWriteSemaphore.wait();

		// Claim a pending request atomically; a direct write may have consumed it.
		writeConfiguration(true);
	}
}
#endif // EFI_FLASH_WRITE_THREAD

static void wakeFlashWriter() {
#if EFI_FLASH_WRITE_THREAD
	if (allowFlashWhileRunning()) {
		flashWriteSemaphore.signal();
	}
#endif
}

void setNeedToWriteConfiguration() {
	efiPrintf("Scheduling configuration write");
	{
		chibios_rt::CriticalSectionLocker lock;
		configurationWriteState.request();
	}
	wakeFlashWriter();
}

bool getNeedToWriteConfiguration() {
	chibios_rt::CriticalSectionLocker lock;
	return configurationWriteState.pending();
}

static bool shouldWriteConfiguration() {
	chibios_rt::CriticalSectionLocker lock;
	return configurationWriteState.shouldWrite();
}

void writeToFlashIfPending() {
	// A failed attempt stays visible as pending, but requires a fresh request.
	if (!allowFlashWhileRunning()) {
		writeConfiguration(true);
	}
}

class BlockingFlashGuard {
public:
	BlockingFlashGuard()
		: m_blocking(!allowFlashWhileRunning()) {
		if (m_blocking) {
			Sensor::inhibitTimeouts(true);
			beginBlockingFlash();
		}
	}

	~BlockingFlashGuard() {
		if (m_blocking) {
			endBlockingFlash();
			Sensor::inhibitTimeouts(false);
		}
	}

private:
	const bool m_blocking;
};

// Keep storage operations here until the integrity/verification change.
template <typename TStorage>
static ConfigurationWriteResult
eraseAndFlashCopy(flashaddr_t storageAddress, const TStorage& data, unsigned completedCopies) {
	if (!storageAddress) {
		return {ConfigurationWritePhase::Layout, storageAddress, FLASH_RETURN_NO_PERMISSION, completedCopies};
	}

	auto err = intFlashErase(storageAddress, sizeof(TStorage));
	if (err != FLASH_RETURN_SUCCESS) {
		return {ConfigurationWritePhase::Erase, storageAddress, err, completedCopies};
	}

	err = intFlashWrite(storageAddress, reinterpret_cast<const char*>(&data), sizeof(TStorage));
	if (err != FLASH_RETURN_SUCCESS) {
		return {ConfigurationWritePhase::Program, storageAddress, err, completedCopies};
	}

	return {ConfigurationWritePhase::Complete, 0, FLASH_RETURN_SUCCESS, completedCopies + 1};
}

bool burnWithoutFlash = false;

void writeToFlashNow() {
	writeConfiguration(false);
}

static void writeConfiguration(bool requestedOnly) {
	{
		chibios_rt::CriticalSectionLocker lock;
		if (!configurationWriteState.begin(requestedOnly)) {
			// Preserve direct requests that arrive during another flash operation.
			if (!requestedOnly) {
				configurationWriteState.request();
			}
			return;
		}
	}

	engine->configBurnTimer.reset();
	ConfigurationWriteResult result{ConfigurationWritePhase::Complete, 0, FLASH_RETURN_SUCCESS, 0};

	if (!burnWithoutFlash) {
		efiPrintf("Writing pending configuration...");
		// No system lock is held while the driver erases, programs or sleeps.
		BlockingFlashGuard guard;

		persistentState.size = sizeof(persistentState);
		persistentState.version = FLASH_DATA_VERSION;
		persistentState.value = flashStateCrc(persistentState);

#if EFI_STORAGE_INT_FLASH == TRUE
		result = eraseAndFlashCopy(getFlashAddrFirstCopy(), persistentState, 0);
		// A known failure of the first copy must leave the backup untouched.
		if (result.success() && getFlashAddrSecondCopy()) {
			result = eraseAndFlashCopy(getFlashAddrSecondCopy(), persistentState, result.completedCopies);
		}
#else
		result = {ConfigurationWritePhase::Layout, 0, FLASH_RETURN_NO_PERMISSION, 0};
#endif
		resetMaxValues();
		{
			chibios_rt::CriticalSectionLocker lock;
			lastWriteResult = result;
		}
		if (result.success()) {
			efiPrintf("FLASH_SUCCESS");
		} else {
			// Set the fatal inhibit before ending the actuator guard. Keep write
			// ownership through error reporting and guard teardown: both can reenter.
			firmwareError(
					"Configuration flash failed: phase %d address 0x%08x error %d copies %u",
					static_cast<int>(result.phase),
					result.address,
					result.error,
					result.completedCopies);
		}
	}
	{
		chibios_rt::CriticalSectionLocker lock;
		lastWriteResult = result;
		configurationWriteState.complete(result.success());
	}

	// A new request may have arrived while the low-priority H7 writer was busy.
	if (shouldWriteConfiguration()) {
		wakeFlashWriter();
	}
}

static void printFlashStatus() {
	ConfigurationWriteResult result;
	bool pending;
	bool writing;
	bool reading;
	{
		chibios_rt::CriticalSectionLocker lock;
		result = lastWriteResult;
		pending = configurationWriteState.pending();
		writing = configurationWriteState.writing();
		reading = configurationWriteState.reading();
	}
	efiPrintf(
			"flash_status pending=%d writing=%d reading=%d phase=%d address=0x%08x error=%d copies=%u",
			pending,
			writing,
			reading,
			static_cast<int>(result.phase),
			result.address,
			result.error,
			result.completedCopies);
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

	intFlashRead(address, (char*)&persistentState, sizeof(persistentState));

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
 * in this method we read first copy of configuration in flash. if that first copy has CRC or other issues we read
 * second copy.
 */
static FlashState readConfiguration() {
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
	bool canRead;
	{
		chibios_rt::CriticalSectionLocker lock;
		canRead = configurationWriteState.beginRead();
	}
	if (!canRead) {
		efiPrintf("Cannot read configuration while another flash operation is active");
		return;
	}
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
			efiPrintf(
					"Resetting due to version mismatch but preserving engine type [%d]",
					(int)engineConfiguration->engineType);
			resetConfigurationExt(engineConfiguration->engineType);
			break;
		case FlashState::Ok:
			// At this point we know that CRC and version number is what we expect. Safe to assume it's a valid
			// configuration.
			applyNonPersistentConfiguration();
			efiPrintf("Read valid configuration from flash!");
			break;
	}

	// we can only change the state after the CRC check
	engineConfiguration->byFirmwareVersion = getRusEfiVersion();
	validateConfiguration();
	{
		chibios_rt::CriticalSectionLocker lock;
		configurationWriteState.endRead();
	}
	if (shouldWriteConfiguration()) {
		wakeFlashWriter();
	}
}

void initFlash() {
	addConsoleAction("readconfig", readFromFlash);
	addConsoleAction("flash_status", printFlashStatus);
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

#if EFI_FLASH_WRITE_THREAD
	if (allowFlashWhileRunning()) {
		chThdCreateStatic(flashWriteStack, sizeof(flashWriteStack), PRIO_FLASH_WRITE, flashWriteThread, nullptr);
	}
#endif
}

#endif /* EFI_INTERNAL_FLASH */
