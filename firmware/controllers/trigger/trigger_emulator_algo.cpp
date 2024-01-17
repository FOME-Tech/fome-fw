/**
 * @file trigger_emulator_algo.cpp
 *
 * This file is about producing real electrical signals which emulate trigger signal based on
 * a known TriggerWaveform.
 *
 * Historically this implementation was implemented based on PwmConfig which is maybe not the
 * best way to implement it. (todo: why is not the best way?)
 *
 * A newer implementation of pretty much the same thing is TriggerStimulatorHelper
 * todo: one emulator should be enough! another one should be eliminated
 *
 * @date Mar 3, 2014
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"

int getPreviousIndex(const int currentIndex, const int size) {
	return (currentIndex + size - 1) % size;
}

bool needEvent(const int currentIndex, const MultiChannelStateSequence & mcss, int channelIndex) {
	int prevIndex = getPreviousIndex(currentIndex, mcss.phaseCount);
	bool previousValue = mcss.getChannelState(channelIndex, /*phaseIndex*/prevIndex);
	bool currentValue = mcss.getChannelState(channelIndex, /*phaseIndex*/currentIndex);

	return previousValue != currentValue;
}

#if EFI_EMULATE_POSITION_SENSORS

#include "trigger_emulator_algo.h"
#include "trigger_central.h"
#include "trigger_simulator.h"

TriggerEmulatorHelper::TriggerEmulatorHelper() {
}

static OutputPin emulatorOutputs[PWM_PHASE_MAX_WAVE_PER_PWM];

void TriggerEmulatorHelper::handleEmulatorCallback(const MultiChannelStateSequence& multiChannelStateSequence, int stateIndex) {
	efitick_t stamp = getTimeNowNt();
	
	// todo: code duplication with TriggerStimulatorHelper::feedSimulatedEvent?
#if EFI_SHAFT_POSITION_INPUT
	for (size_t i = 0; i < PWM_PHASE_MAX_WAVE_PER_PWM; i++) {
		if (needEvent(stateIndex, multiChannelStateSequence, i)) {
			bool isRise = multiChannelStateSequence.getChannelState(/*phaseIndex*/i, stateIndex);

			isRise ^= (i == 0 && engineConfiguration->invertPrimaryTriggerSignal);
			isRise ^= (i == 1 && engineConfiguration->invertSecondaryTriggerSignal);

			handleShaftSignal(i, isRise, stamp);
		}
	}
#endif // EFI_SHAFT_POSITION_INPUT
}

PwmConfig triggerSignal;

static int atTriggerVersion = 0;

/**
 * todo: why is this method NOT reciprocal to getCrankDivider?!
 * todo: oh this method has only one usage? there must me another very similar method!
 */
static float getRpmMultiplier(operation_mode_e mode) {
	switch (mode) {
	case FOUR_STROKE_SYMMETRICAL_CRANK_SENSOR:
	case FOUR_STROKE_THREE_TIMES_CRANK_SENSOR:
	case FOUR_STROKE_TWELVE_TIMES_CRANK_SENSOR:
	case FOUR_STROKE_CRANK_SENSOR:
	case FOUR_STROKE_CAM_SENSOR:
	case OM_NONE:
		return getCrankDivider(mode) / 2.0;
	case TWO_STROKE:
		// unit test coverage still runs if the value below is changed to '2' not a great sign!
		// but HW CI insists that we have '1' here
		return 1;
	};

	return 1;
}

void setTriggerEmulatorRPM(int rpm) {
	engineConfiguration->triggerSimulatorRpm = rpm;
	/**
	 * All we need to do here is to change the periodMs
	 * togglePwmState() would see that the periodMs has changed and act accordingly
	 */
	if (rpm == 0) {
		triggerSignal.setFrequency(NAN);
	} else {
		float rpmM = getRpmMultiplier(getEngineRotationState()->getOperationMode());
		float rPerSecond = rpm * rpmM / 60.0; // per minute converted to per second
		triggerSignal.setFrequency(rPerSecond);
	}
	engine->resetEngineSnifferIfInTestMode();

	efiPrintf("Emulating position sensor(s). RPM=%d", rpm);
}

static void updateTriggerWaveformIfNeeded(PwmConfig *state) {
	if (atTriggerVersion < engine->triggerCentral.triggerShape.version) {
		atTriggerVersion = engine->triggerCentral.triggerShape.version;
		efiPrintf("Stimulator: updating trigger shape: %d/%d", atTriggerVersion,
				engine->getGlobalConfigurationVersion());

		TriggerWaveform *s = &engine->triggerCentral.triggerShape;
		copyPwmParameters(state, &s->wave);
		state->safe.periodNt = -1; // this would cause loop re-initialization
	}
}

static TriggerEmulatorHelper helper;
static bool hasStimPins = false;

static bool hasInitTriggerEmulator = false;

# if !EFI_UNIT_TEST

static void emulatorApplyPinState(int stateIndex, PwmConfig *state) /* pwm_gen_callback */ {
	if (engine->triggerCentral.directSelfStimulation) {
		/**
		 * this callback would invoke the input signal handlers directly
		 */
		helper.handleEmulatorCallback(
				*state->multiChannelStateSequence,
				stateIndex);
	}

#if EFI_PROD_CODE
	// Only set pins if they're configured - no need to waste the cycles otherwise
	else if (hasStimPins) {
		applyPinState(stateIndex, state);
	}
#endif /* EFI_PROD_CODE */
}

static void startSimulatedTriggerSignal() {
	// No need to start more than once
	if (hasInitTriggerEmulator) {
		return;
	}

	TriggerWaveform *s = &engine->triggerCentral.triggerShape;
	setTriggerEmulatorRPM(engineConfiguration->triggerSimulatorRpm);
	triggerSignal.weComplexInit(
			&engine->executor,
			&s->wave,
			updateTriggerWaveformIfNeeded, (pwm_gen_callback*)emulatorApplyPinState);

	hasInitTriggerEmulator = true;
}

void enableTriggerStimulator() {
	startSimulatedTriggerSignal();
	engine->triggerCentral.directSelfStimulation = true;
    engine->rpmCalculator.Register();
    incrementGlobalConfigurationVersion();
}

void enableExternalTriggerStimulator() {
	startSimulatedTriggerSignal();
	engine->triggerCentral.directSelfStimulation = false;
    incrementGlobalConfigurationVersion();
}

void disableTriggerStimulator() {
	engine->triggerCentral.directSelfStimulation = false;
	triggerSignal.stop();
	hasInitTriggerEmulator = false;
    incrementGlobalConfigurationVersion();
}

void initTriggerEmulatorLogic() {
	addConsoleActionI(CMD_RPM, setTriggerEmulatorRPM);
}

void onConfigurationChangeRpmEmulatorCallback(engine_configuration_s *previousConfiguration) {
	if (engineConfiguration->triggerSimulatorRpm ==
			previousConfiguration->triggerSimulatorRpm) {
		return;
	}
	setTriggerEmulatorRPM(engineConfiguration->triggerSimulatorRpm);
}

void initTriggerEmulator() {
	efiPrintf("Emulating %s", getEngine_type_e(engineConfiguration->engineType));

	startTriggerEmulatorPins();

	initTriggerEmulatorLogic();
}

#endif /* EFI_UNIT_TEST */

void startTriggerEmulatorPins() {
	hasStimPins = false;
	for (size_t i = 0; i < efi::size(emulatorOutputs); i++) {
		triggerSignal.outputPins[i] = &emulatorOutputs[i];

		brain_pin_e pin = engineConfiguration->triggerSimulatorPins[i];

		// Only bother trying to set output pins if they're configured
		if (isBrainPinValid(pin)) {
			hasStimPins = true;
		}

#if EFI_PROD_CODE
		if (isConfigurationChanged(triggerSimulatorPins[i])) {
			triggerSignal.outputPins[i]->initPin("Trigger emulator", pin);
		}
#endif // EFI_PROD_CODE
	}
}

void stopTriggerEmulatorPins() {
#if EFI_PROD_CODE
	for (size_t i = 0; i < efi::size(emulatorOutputs); i++) {
		if (isConfigurationChanged(triggerSimulatorPins[i])) {
			triggerSignal.outputPins[i]->deInit();
		}
	}
#endif // EFI_PROD_CODE
}

#endif /* EFI_EMULATE_POSITION_SENSORS */
