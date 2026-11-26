/**
 * @file config_steps.cpp
 *
 * The configuration sequence: what's set up from the configuration, in dependency order. Startup and
 * burn both run through this list, see config_sequence.h.
 *
 * Subsystems not listed here yet are still started by their init functions in initHardware() and
 * commonInitEngineController(), and reconfigured (or not) by applyNewHardwareSettings() and the
 * onConfigurationChange hooks.
 */

#include "pch.h"

#include "config_steps.h"
#include "hardware.h"
#include "init.h"
#include "can_hw.h"
#include "can_vss.h"
#include "can_imu.h"
#include "software_knock.h"
#include "tachometer.h"
#include "speedometer.h"
#include "alternator_controller.h"
#include "fan_control.h"
#include "start_stop.h"
#include "gppwm.h"
#include "boost_control.h"
#include "vvt.h"

static constexpr ConfigStep configSteps[] = {
// Hardware phase: initHardware()
#if EFI_SOFTWARE_KNOCK
		{"software knock",
		 BootPhase::Hardware,
		 ApplyWhen::RebootOnly,
		 nullptr,
		 startSoftwareKnock,
		 softwareKnockNeedsReboot},
#endif // EFI_SOFTWARE_KNOCK

#if EFI_CAN_SUPPORT
		{"CAN", BootPhase::Hardware, ApplyWhen::RebootOnly, nullptr, startCan, canNeedsReboot},
		// Registers vehicle speed like the wired VSS in "sensors" does. Being earlier in the list, it stops
		// after and starts before the sensors, so either can hand vehicle speed to the other in one burn.
		{"CAN VSS", BootPhase::Hardware, ApplyWhen::Immediately, stopCanVss, startCanVss, nullptr},
		{"CAN IMU", BootPhase::Hardware, ApplyWhen::Immediately, stopCanImu, startCanImu, nullptr},
#endif // EFI_CAN_SUPPORT

// Controller phase: commonInitEngineController()
#if EFI_ENGINE_CONTROL
		{"cylinder layout", BootPhase::Controller, ApplyWhen::Immediately, nullptr, startCylinderLayout, nullptr},
#endif // EFI_ENGINE_CONTROL

// Unit tests mock sensors individually instead
#if !EFI_UNIT_TEST
		{"sensors",
		 BootPhase::Controller,
		 ApplyWhen::Immediately,
		 stopConfiguredSensors,
		 startConfiguredSensors,
		 nullptr},
		// Follows the MAP sensor's input
		{"fast ADC channels", BootPhase::Controller, ApplyWhen::Immediately, nullptr, startFastAdcChannels, nullptr},
#endif // EFI_UNIT_TEST

		{"GPPWM", BootPhase::Controller, ApplyWhen::Immediately, stopGpPwm, startGpPwm, nullptr},
		{"tachometer", BootPhase::Controller, ApplyWhen::Immediately, stopTachometer, startTachometer, nullptr},
		{"speedometer", BootPhase::Controller, ApplyWhen::Immediately, stopSpeedometer, startSpeedometer, nullptr},

#if EFI_ALTERNATOR_CONTROL
		{"alternator output",
		 BootPhase::Controller,
		 ApplyWhen::Immediately,
		 stopAlternatorOutput,
		 startAlternatorOutput,
		 nullptr},
#endif // EFI_ALTERNATOR_CONTROL

// Unit tests give the fan controllers a mock PWM instead
#if EFI_ENGINE_CONTROL && !EFI_UNIT_TEST
		{"fan PWM", BootPhase::Controller, ApplyWhen::Immediately, stopFanPwm, startFanPwm, nullptr},
#endif

#if EFI_ENGINE_CONTROL
		{"boost control", BootPhase::Controller, ApplyWhen::Immediately, stopBoostControl, startBoostControl, nullptr},
#endif

#if EFI_VVT_PID
		{"VVT outputs", BootPhase::Controller, ApplyWhen::Immediately, stopVvtOutputs, startVvtOutputs, nullptr},
#endif

		{"start/stop button", BootPhase::Controller, ApplyWhen::Immediately, nullptr, startStartStopButton, nullptr},
};

static ConfigSequence sequence(configSteps);

#if !EFI_UNIT_TEST
static chibios_rt::Mutex configChangeMutex;

ConfigChangeLock::ConfigChangeLock() {
	configChangeMutex.lock();
}

ConfigChangeLock::~ConfigChangeLock() {
	configChangeMutex.unlock();
}
#else
ConfigChangeLock::ConfigChangeLock() {}
ConfigChangeLock::~ConfigChangeLock() {}
#endif

static void publishStatus() {
#if EFI_TUNER_STUDIO
	engine->outputChannels.rebootRequired = sequence.isRebootRequired();
#endif
}

static bool startupFinished = false;

void configStepsBoot(BootPhase phase) {
	sequence.boot(phase);
	publishStatus();
}

void configStepsStartupFinished() {
	startupFinished = true;
}

bool isStartupFinished() {
	return startupFinished;
}

#if EFI_UNIT_TEST
void configStepsReset() {
	startupFinished = false;
}
#endif

void configStepsBeginChange(const engine_configuration_s* prev) {
	sequence.beginChange(prev);
}

void configStepsFinishChange(const engine_configuration_s* prev) {
	sequence.finishChange(prev);

	if (sequence.isRebootRequired()) {
		efiPrintf("Some configuration changes need a reboot to take effect");
	}

	publishStatus();
}

bool isConfigRebootRequired() {
	return sequence.isRebootRequired();
}
