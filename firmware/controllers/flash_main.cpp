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
#include "configuration_storage.h"
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

// CRC readback uses a chunk buffer; allow room for the driver and diagnostics.
static THD_WORKING_AREA(flashWriteStack, 2 * UTILITY_THREAD_STACK_SIZE);

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
		result = writeConfigurationCopies(getFlashAddrFirstCopy(), getFlashAddrSecondCopy(), persistentState);
#else
		result = {ConfigurationWritePhase::Layout, 0, FLASH_RETURN_NO_PERMISSION, 0};
#endif
		if (result.success()) {
			efiPrintf("FLASH_SUCCESS");
		} else {
			firmwareError(
					"Configuration flash failed: phase %d address 0x%08x error %d verified %u",
					static_cast<int>(result.phase),
					result.address,
					result.error,
					result.verifiedCopies);
		}
		resetMaxValues();
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
	{
		chibios_rt::CriticalSectionLocker lock;
		result = lastWriteResult;
		pending = configurationWriteState.pending();
		writing = configurationWriteState.writing();
	}
	efiPrintf(
			"flash_status pending=%d writing=%d phase=%d address=0x%08x error=%d verified=%u",
			pending,
			writing,
			static_cast<int>(result.phase),
			result.address,
			result.error,
			result.verifiedCopies);
}

static void doResetConfiguration() {
	resetConfigurationExt(engineConfiguration->engineType);
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
#if EFI_STORAGE_INT_FLASH == TRUE
	auto result = readConfigurationCopies(getFlashAddrFirstCopy(), getFlashAddrSecondCopy(), persistentState);
#else
	auto result = ConfigurationFlashState::Ok;
#endif

	switch (result) {
		case ConfigurationFlashState::ReadFailed:
		case ConfigurationFlashState::CrcFailed:
			warning(ObdCode::CUSTOM_ERR_FLASH_CRC_FAILED, "flash CRC failed");
			efiPrintf("Need to reset flash to default due to CRC mismatch");
			[[fallthrough]];
		case ConfigurationFlashState::BlankChip:
			resetConfigurationExt(engine_type_e::DEFAULT_ENGINE_TYPE);
			break;
		case ConfigurationFlashState::IncompatibleVersion:
			// Preserve engine type from old config
			efiPrintf(
					"Resetting due to version mismatch but preserving engine type [%d]",
					(int)engineConfiguration->engineType);
			resetConfigurationExt(engineConfiguration->engineType);
			break;
		case ConfigurationFlashState::Ok:
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
