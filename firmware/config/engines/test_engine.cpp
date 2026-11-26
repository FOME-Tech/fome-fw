/**
 * @file	test_engine.cpp
 *
 * set engine_type 26
 * VVT
 * set engine_type 45
 *
 * See also custom_engine.cpp
 *
 * @date Nov 14, 2014
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"
#include "test_engine.h"

// TEST_ENGINE
void setTestCamEngineConfiguration() {
	engineConfiguration->trigger.type = trigger_type_e::TT_ONE_PLUS_ONE;
	setCamOperationMode();

	//	engineConfiguration->trigger.type = trigger_type_e::TT_TOOTHED_WHEEL;
	//	trigger_config_s *triggerConfig = &engineConfiguration->trigger;
	//	triggerConfig->customTotalToothCount = 60;
	//	triggerConfig->customSkippedToothCount = 0;

	engineConfiguration->mafAdcChannel = EFI_ADC_1;
	engineConfiguration->tps1_1AdcChannel = EFI_ADC_2;
	engineConfiguration->vbattAdcChannel = EFI_ADC_NONE;

	setWholeIatCorrTimingTable(0);

	// Many tests were written when the default target AFR was 14.0, so use that for tests by default.
	engineConfiguration->stoichRatioPrimary = 14.0f;

	engineConfiguration->ignitionMode = IM_ONE_COIL;
	setConstantDwell(3); // 50% duty cycle @ 5000 rpm

	engineConfiguration->malfunctionIndicatorPin = Gpio::Unassigned;

	engineConfiguration->ignitionPins[0] = Gpio::C7;		 // #1
	engineConfiguration->ignitionPins[1] = Gpio::Unassigned; // #2
	engineConfiguration->ignitionPins[2] = Gpio::Unassigned; // #3
	engineConfiguration->ignitionPins[3] = Gpio::Unassigned; // #4
	engineConfiguration->ignitionPins[4] = Gpio::Unassigned; // #5
	engineConfiguration->ignitionPins[5] = Gpio::Unassigned; // #6
}

void setTestCrankEngineConfiguration() {
	setTestCamEngineConfiguration();

	engineConfiguration->trigger.type = trigger_type_e::TT_ONE;

	setCrankOperationMode();
	// this is related to 'setDefaultBaseEngine' having 'skippedWheelOnCam = true' which is a weird fact by itself
	engineConfiguration->skippedWheelOnCam = false;
}

void setTestDcWastegateConfiguration() {
	engineConfiguration->isBoostControlEnabled = 1;
	engineConfiguration->etbFunctions[0] = DC_Wastegate;
	setTable(config->boostTableOpenLoop, 50);
	engineConfiguration->tps1_1AdcChannel = EFI_ADC_1; // PA1

	// PWM pin
	engineConfiguration->etbIo[0].controlPin = Gpio::C0;
	// DIR pin
	engineConfiguration->etbIo[0].directionPin1 = Gpio::C1;
	// Disable pin
	engineConfiguration->etbIo[0].disablePin = Gpio::C2;
	// we only have pwm/dir, no dira/dirb
	engineConfiguration->etb_use_two_wires = false;
}

void setTestVVTEngineConfiguration() {
	setCrankOperationMode();

	engineConfiguration->trigger.type = trigger_type_e::TT_TOOTHED_WHEEL;
	engineConfiguration->trigger.customTotalToothCount = 3;
	engineConfiguration->trigger.customSkippedToothCount = 1;

	// set algorithm 3
	setAlgorithm(LM_SPEED_DENSITY);

	engineConfiguration->triggerInputPins[1] = Gpio::Unassigned;
	engineConfiguration->camInputs[0] = Gpio::A5;

	// set global_trigger_offset_angle 0
	engineConfiguration->globalTriggerAngleOffset = 0;

	engineConfiguration->vvtMode[0] = VVT_SINGLE_TOOTH;
}

#if EFI_UNIT_TEST
void setTestEngineIssue366both() {
	setTestCamEngineConfiguration();

	engineConfiguration->trigger.customTotalToothCount = 2;
	engineConfiguration->trigger.customSkippedToothCount = 1;

	engineConfiguration->trigger.type = trigger_type_e::TT_TOOTHED_WHEEL;
}

void setTestEngineIssue366rise() {
	setTestEngineIssue366both();
}
#endif /* EFI_UNIT_TEST */

#if defined(HARDWARE_CI) && (HW_PROTEUS || HW_ATLAS)
#include "polygonus_meta.h"

/**
 * The HW CI loopback rig, the same on Proteus and Atlas since it's built from their shared
 * connector names. The ECU's outputs are jumpered back to its own inputs:
 *  - Ign 10 -> 1k -> Analog Volt 2 (100uF to ground): output duty shows up as voltage, read as TPS1
 *  - Ign 11 -> Digital 1, Ign 12 -> Digital 2: trigger simulator into the crank and cam inputs
 *  - Ign 9 -> Digital 3, Ign 8 -> Digital 5: frequency loops, left free for tests to assign
 *  - CAN1 <-> CAN2: both buses are on at 500k by default, nothing to set
 */
void setHwCiRig() {
	// 4 cylinders: only coils 1..cylinder count are initialized
	engineConfiguration->firingOrder = FO_1_3_4_2;

	// The rig's outputs aren't coils, even if a test raises the cylinder count
	static const Gpio rigOutputs[] = { PROTEUS_IGN_8, PROTEUS_IGN_9, PROTEUS_IGN_10, PROTEUS_IGN_11, PROTEUS_IGN_12 };
	for (auto& pin : engineConfiguration->ignitionPins) {
		for (Gpio rigOutput : rigOutputs) {
			if (pin == rigOutput) {
				pin = Gpio::Unassigned;
			}
		}
	}

	// Lowest CPU trigger possible: the analog test only runs the stimulator to make the idle valve work.
	// Tests that need a real trigger pattern set their own.
	engineConfiguration->trigger.type = trigger_type_e::TT_ONE;
	engineConfiguration->triggerSimulatorPins[0] = PROTEUS_IGN_11;
	engineConfiguration->triggerSimulatorPins[1] = PROTEUS_IGN_12;
	engineConfiguration->triggerInputPins[0] = PROTEUS_DIGITAL_1;

	// The idle valve output is the analog loop's source, read back as TPS1
	engineConfiguration->idle.solenoidPin = PROTEUS_IGN_10;
	engineConfiguration->tps1_1AdcChannel = PROTEUS_IN_ANALOG_VOLT_2;

	// 5893hz is coprime with the analog sample rate, 500hz, so hopefully we get less aliasing
	engineConfiguration->idle.solenoidFrequency = 5893;

	// Test range is 20% to 80%
	engineConfiguration->tpsMin = 200;
	engineConfiguration->tpsMax = 800;
}
#endif
