/**
 * @file    idle_hardware.cpp
 * @brief   Idle Air Control valve hardware
 *
 * @date November 3, 2020
 *
 * This is just the hardware interface - deciding where to put the valve happens in idle_thread.cpp
 */

#include "pch.h"

#if EFI_IDLE_CONTROL
#include "idle_hardware.h"

#include "electronic_throttle.h"

#include "dc_motors.h"
#if !EFI_UNIT_TEST
#include "stepper.h"
/* Storing two following structs in CCM memory cause HardFault (at least on F4)
 * This need deep debuging. Until it is moved out of CMM. */
static StepDirectionStepper iacStepperHw;
static DualHBridgeStepper iacHbridgeHw;
StepperMotor iacMotor CCM_OPTIONAL;
#endif /* EFI_UNIT_TEST */

static SimplePwm idleSolenoidOpen("idle open");
static SimplePwm idleSolenoidClose("idle close");

void applyIACposition(percent_t position) {
	/**
	 * currently idle level is an percent value (0-100 range), and PWM takes a float in the 0..1 range
	 * todo: unify?
	 */
	float duty = PERCENT_TO_DUTY(position);

#if EFI_ELECTRONIC_THROTTLE_BODY
	setEtbIdlePosition(position);
#endif // EFI_ELECTRONIC_THROTTLE_BODY

	if (engineConfiguration->useStepperIdle) {
#if !EFI_UNIT_TEST
		iacMotor.setTargetPosition(duty * engineConfiguration->idleStepperTotalSteps);
#endif
	} else {
		// if not spinning or running a bench test, turn off the idle valve(s) to be quieter and save power
#if EFI_SHAFT_POSITION_INPUT
		if (!engine->triggerCentral.engineMovedRecently() && engine->timeToStopIdleTest == 0) {
			idleSolenoidOpen.setSimplePwmDutyCycle(0);
			idleSolenoidClose.setSimplePwmDutyCycle(0);
			return;
		}
#endif // EFI_SHAFT_POSITION_INPUT

		if (!engineConfiguration->isDoubleSolenoidIdle) {
			idleSolenoidOpen.setSimplePwmDutyCycle(duty);
		} else {
			// use 0.01..0.99 range
			float idle_range = 0.98; // move to config?

			float idle_open = 0.01 + idle_range * duty;
			float idle_close = 0.01 + idle_range * (1.0 - duty);

			idleSolenoidOpen.setSimplePwmDutyCycle(idle_open);
			idleSolenoidClose.setSimplePwmDutyCycle(idle_close);
		}
	}
}

#if !EFI_UNIT_TEST

bool isIdleMotorBusy() {
	if (!engineConfiguration->useStepperIdle) {
		// todo: check other motor types?
		return false;
	}
	return iacMotor.isBusy();
}

enum class IdleValve : uint8_t {
	None,
	Solenoid,
	// Step/direction stepper driver
	Stepper,
	// Stepper on two DC motor outputs (H-bridges, or raw outputs)
	DcStepper,
};

static IdleValve getWantedValve() {
	if (engineConfiguration->useStepperIdle) {
		if (engineConfiguration->useRawOutputToDriveIdleStepper || engineConfiguration->useHbridgesToDriveIdleStepper) {
			return IdleValve::DcStepper;
		}

		return IdleValve::Stepper;
	}

	if (isBrainPinValid(engineConfiguration->idle.solenoidPin)) {
		return IdleValve::Solenoid;
	}

	return IdleValve::None;
}

// The solenoid pins are EnginePins RegisteredOutputPins, claimed and moved by every burn
static ConfiguredPwm solenoidOpenOutput(idleSolenoidOpen);
static ConfiguredPwm solenoidCloseOutput(idleSolenoidClose);

static bool isSolenoidOpenWanted() {
	return getWantedValve() == IdleValve::Solenoid && engineConfiguration->idle.solenoidFrequency >= 1;
}

static bool isSolenoidCloseWanted() {
	return isSolenoidOpenWanted() && engineConfiguration->isDoubleSolenoidIdle &&
		   isBrainPinValid(engineConfiguration->secondSolenoidPin);
}

// Whether iacMotor has been given hardware (and so its thread started)
static bool motorHasHardware = false;
// Whether iacStepperHw holds its pins
static bool stepperPinsSet = false;
// DC motor hardware can't be stopped once started (see DcHardware::start()), so a DC stepper is only set
// up once. Its settings take a power cycle to change (requiresPowerCycle in the ini).
static bool dcStepperSet = false;

static bool stepperPinsChanged(const engine_configuration_s* prev) {
	return !prev || prev->idle.stepperStepPin != engineConfiguration->idle.stepperStepPin ||
		   prev->idle.stepperDirectionPin != engineConfiguration->idle.stepperDirectionPin ||
		   prev->stepperDirectionPinMode != engineConfiguration->stepperDirectionPinMode ||
		   prev->stepperEnablePin != engineConfiguration->stepperEnablePin ||
		   prev->stepperEnablePinMode != engineConfiguration->stepperEnablePinMode;
}

void stopIdleHardware(const engine_configuration_s* prev) {
	solenoidOpenOutput.stopIfUnwanted(isSolenoidOpenWanted());
	solenoidCloseOutput.stopIfUnwanted(isSolenoidCloseWanted());

	auto wanted = getWantedValve();

	if (motorHasHardware && wanted != IdleValve::Stepper && wanted != IdleValve::DcStepper) {
		// The stepper thread keeps running, without hardware it does nothing
		iacMotor.initialize(nullptr, engineConfiguration->idleStepperTotalSteps);
		motorHasHardware = false;
	}

	if (wanted != IdleValve::DcStepper && dcStepperSet) {
		iacHbridgeHw.disable();
	}

	if (stepperPinsSet && (wanted != IdleValve::Stepper || stepperPinsChanged(prev))) {
		// Releases the pins without claiming new ones
		iacStepperHw.initialize(
				Gpio::Unassigned,
				Gpio::Unassigned,
				OM_DEFAULT,
				engineConfiguration->idleStepperReactionTime,
				Gpio::Unassigned,
				OM_DEFAULT);
		stepperPinsSet = false;
	}
}

void startIdleHardware(const engine_configuration_s* prev) {
	switch (getWantedValve()) {
		case IdleValve::None:
			break;
		case IdleValve::Solenoid: {
			float frequency = engineConfiguration->idle.solenoidFrequency;
			float duty = PERCENT_TO_DUTY(engineConfiguration->manIdlePosition);

			if (engineConfiguration->isDoubleSolenoidIdle && !isBrainPinValid(engineConfiguration->secondSolenoidPin)) {
				firmwareError("Second idle pin should be configured for double solenoid mode.");
			}

			// todo: even for double-solenoid mode we can probably use same single SimplePWM
			solenoidOpenOutput.startIfWanted(
					prev, isSolenoidOpenWanted(), "Idle Valve Open", &enginePins.idleSolenoidPin, frequency, duty);
			solenoidCloseOutput.startIfWanted(
					prev,
					isSolenoidCloseWanted(),
					"Idle Valve Close",
					&enginePins.secondIdleSolenoidPin,
					frequency,
					duty);

			// Already running: follow a frequency change
			if (solenoidOpenOutput.isRunning()) {
				idleSolenoidOpen.setFrequency(frequency);
			}

			if (solenoidCloseOutput.isRunning()) {
				idleSolenoidClose.setFrequency(frequency);
			}
		} break;
		case IdleValve::Stepper:
			if (!stepperPinsSet) {
				iacStepperHw.initialize(
						engineConfiguration->idle.stepperStepPin,
						engineConfiguration->idle.stepperDirectionPin,
						engineConfiguration->stepperDirectionPinMode,
						engineConfiguration->idleStepperReactionTime,
						engineConfiguration->stepperEnablePin,
						engineConfiguration->stepperEnablePinMode);
				stepperPinsSet = true;
			} else {
				iacStepperHw.setReactionTime(engineConfiguration->idleStepperReactionTime);
			}

			iacMotor.initialize(&iacStepperHw, engineConfiguration->idleStepperTotalSteps);
			motorHasHardware = true;
			break;
		case IdleValve::DcStepper:
			if (!dcStepperSet) {
				DcMotor* motorA;
				DcMotor* motorB;

				if (engineConfiguration->useRawOutputToDriveIdleStepper) {
					motorA = initDcMotor(
							engineConfiguration->stepper_raw_output[0],
							engineConfiguration->stepper_raw_output[1],
							ETB_COUNT + 0);
					motorB = initDcMotor(
							engineConfiguration->stepper_raw_output[2],
							engineConfiguration->stepper_raw_output[3],
							ETB_COUNT + 1);
				} else {
					motorA = initDcMotor(engineConfiguration->stepperDcIo[0], ETB_COUNT + 0, /*useTwoWires*/ true);
					motorB = initDcMotor(engineConfiguration->stepperDcIo[1], ETB_COUNT + 1, /*useTwoWires*/ true);
				}

				iacHbridgeHw.initialize(motorA, motorB, engineConfiguration->idleStepperReactionTime);
				dcStepperSet = true;
			} else {
				iacHbridgeHw.setReactionTime(engineConfiguration->idleStepperReactionTime);
			}

			// In case a switch to another valve type disabled it
			iacHbridgeHw.enable();
			iacMotor.initialize(&iacHbridgeHw, engineConfiguration->idleStepperTotalSteps);
			motorHasHardware = true;
			break;
	}
}
#endif

#endif // EFI_IDLE_HARDWARE
