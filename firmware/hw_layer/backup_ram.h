/**
 * @file	backup_ram.h
 * @brief	Non-volatile backup-RAM registers support
 *
 * @date Dec 19, 2017
 */

#pragma once

#include "efi_gpio.h"

#include "error_handling.h"

// These use very specific values to avoid interpreting random garbage memory as a real value
enum class ErrorCookie : uint32_t {
	None = 0,
	FirmwareError = 0xcafebabe,
	HardFault = 0xdeadbeef,
};

struct BackupSramData {
	static const uint32_t ExpectedCookie = 0xDEADBEEF;
	uint32_t Cookie = ExpectedCookie;
	uint32_t Version = FLASH_DATA_VERSION;

	// Error handling/recovery/reporting information
	struct {
		ErrorCookie Cookie;

		critical_msg_t ErrorString;
		port_extctx FaultCtx;
		uint32_t FaultType;
		uint32_t FaultAddress;
		uint32_t Csfr;

		uint32_t BootCount;
		uint32_t BootCountCookie;
	} Err;

	/**
	 * IAC Stepper motor position
	 * Used in stepper.cpp
	 */
	int StepperPosition = 0;

	/**
	 * Ignition switch counter, 8-bit (stored in BKP0R 16..23)
	 * The counter stores the number of times the ignition switch is turned on. Used for prime injection pulse.
	 * We need a protection against 'fake' ignition switch on and off (i.e. no engine started), to avoid repeated prime
	 * pulses. So we check and update the ignition switch counter in non-volatile backup-RAM. See
	 * startPrimeInjectionPulse() in controllers/trigger/main_trigger_callback.cpp
	 */
	uint16_t IgnCounter = 0;

	// Persisent values stored/read by Lua scripts
	float LuaPersistentData[64] = {0};

	/**
	 * Last valid reading from the flex fuel sensor.
	 * The fuel in the tank can't change while we aren't looking (that takes both refueling and
	 * enough running to flow new fuel past the sensor), so this is a good guess at ethanol content
	 * until the sensor wakes up after a restart, or forever if it has failed.
	 * Negative means we've never seen a valid reading.
	 */
	float FlexEthanolPct = -1;
};

BackupSramData* getBackupSram();
