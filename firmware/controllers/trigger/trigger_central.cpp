/*
 * @file	trigger_central.cpp
 * Here we have a bunch of higher-level methods which are not directly related to actual signal decoding
 *
 * @date Feb 23, 2014
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"

#include "trigger_central.h"
#include "trigger_decoder.h"
#include "main_trigger_callback.h"
#include "listener_array.h"
#include "logic_analyzer.h"

#include "local_version_holder.h"
#include "trigger_simulator.h"
#include "trigger_emulator_algo.h"

#include "main_trigger_callback.h"
#include "status_loop.h"
#include "engine_sniffer.h"
#include "auto_generated_sync_edge.h"

#if EFI_TUNER_STUDIO
#include "tunerstudio.h"
#endif /* EFI_TUNER_STUDIO */

#if EFI_ENGINE_SNIFFER
WaveChart waveChart;
#endif /* EFI_ENGINE_SNIFFER */

#define TRIGGER_WAVEFORM(x) getTriggerCentral()->triggerShape.x

#if EFI_SHAFT_POSITION_INPUT

EngPhase TriggerCentral::toEngPhase(const TrgPhase& trgPhase) const {
	// Adjust so currentPhase is in engine-space angle, not trigger-space angle
	return { wrapAngleMethod(
		trgPhase.angle
			- triggerShape.tdcPosition
			- engineConfiguration->globalTriggerAngleOffset
			+ triggerState.m_phaseAdjustment,
		"currentEnginePhase", ObdCode::CUSTOM_ERR_6555) };
}

TrgPhase TriggerCentral::toTrgPhase(const EngPhase& engPhase) const {
	return { wrapAngleMethod(
		engPhase.angle
			+ triggerShape.tdcPosition
			+ engineConfiguration->globalTriggerAngleOffset
			- triggerState.m_phaseAdjustment,
		"currentEnginePhase", ObdCode::CUSTOM_ERR_6555) };
}

TriggerCentral::TriggerCentral() :
		vvtEventRiseCounter(),
		vvtEventFallCounter(),
		vvtPosition(),
		triggerState("TRG")
{
	resetCounters();
	triggerState.resetState();
}

expected<angle_t> TriggerCentral::getVVTPosition(uint8_t bankIndex, uint8_t camIndex) {
	if (bankIndex >= BANKS_COUNT || camIndex >= CAMS_PER_BANK) {
		return unexpected;
	}

	auto& vvt = vvtPosition[bankIndex][camIndex];

	if (vvt.t.hasElapsedSec(1)) {
		// 1s timeout on VVT angle
		return unexpected;
	} else {
		return vvt.angle;
	}
}

/**
 * @return angle since trigger synchronization point, NOT angle since TDC.
 */
expected<TrgPhase> TriggerCentral::getCurrentEnginePhase(efitick_t nowNt) const {
	floatus_t oneDegreeUs = engine->rpmCalculator.oneDegreeUs;

	if (std::isnan(oneDegreeUs)) {
		return unexpected;
	}

	float elapsed;
	TrgPhase toothPhase;

	{
		// under lock to avoid mismatched tooth phase and time
		chibios_rt::CriticalSectionLocker csl;

		elapsed = m_lastToothTimer.getElapsedUs(nowNt);
		toothPhase = m_lastToothPhaseFromSyncPoint;
	}

	return toothPhase + elapsed / oneDegreeUs;
}

/**
 * todo: why is this method NOT reciprocal to getRpmMultiplier?!
 */
int getCrankDivider(operation_mode_e operationMode) {
	switch (operationMode) {
	case FOUR_STROKE_CRANK_SENSOR:
		return 2;
	case FOUR_STROKE_SYMMETRICAL_CRANK_SENSOR:
		return SYMMETRICAL_CRANK_SENSOR_DIVIDER;
	case FOUR_STROKE_THREE_TIMES_CRANK_SENSOR:
		return SYMMETRICAL_THREE_TIMES_CRANK_SENSOR_DIVIDER;
	case FOUR_STROKE_TWELVE_TIMES_CRANK_SENSOR:
		return SYMMETRICAL_TWELVE_TIMES_CRANK_SENSOR_DIVIDER;
	case OM_NONE:
	case FOUR_STROKE_CAM_SENSOR:
	case TWO_STROKE:
		// That's easy - trigger cycle matches engine cycle
		return 1;
	}

	firmwareError(ObdCode::OBD_PCM_Processor_Fault, "unexpected operationMode in getCrankDivider");

	return 1;
}

static bool vvtWithRealDecoder(vvt_mode_e vvtMode) {
	// todo: why does VVT_TOYOTA_3_TOOTH not use real decoder?
	return vvtMode != VVT_INACTIVE
			&& vvtMode != VVT_TOYOTA_3_TOOTH
			&& vvtMode != VVT_HONDA_K_INTAKE
			&& vvtMode != VVT_MAP_V_TWIN
			&& vvtMode != VVT_SINGLE_TOOTH;
}

angle_t TriggerCentral::syncAndReport(int divider, int remainder) {
	angle_t engineCycle = getEngineCycle(getEngineRotationState()->getOperationMode());

	return triggerState.syncEnginePhase(divider, remainder, engineCycle);
}

static angle_t adjustCrankPhase(int camIndex) {
	{
		float rpm = Sensor::getOrZero(SensorType::Rpm);

		float minSyncThreshold = engineConfiguration->minCamPhaseResolveRpm;
		bool isBelowMinSync = minSyncThreshold != 0 && rpm < minSyncThreshold;

		float maxSyncThreshold = engineConfiguration->maxCamPhaseResolveRpm;
		bool isAboveMaxSync = maxSyncThreshold != 0 && rpm > maxSyncThreshold;

		if (isBelowMinSync || isAboveMaxSync) {
			// The user has elected to stop trying to resolve crank phase above/below some RPM.
			// Maybe their cam sensor only works at low RPM, high RPM, or something.
			// Anyway, don't try to change crank phase at all, and return that we made no change.
			return 0;
		}
	}

	operation_mode_e operationMode = getEngineRotationState()->getOperationMode();

	auto crankDivider = getCrankDivider(operationMode);
	if (crankDivider == 1) {
		// Crank divider of 1 means there's no ambiguity, so don't try to resolve it
		return 0;
	}

	TriggerCentral *tc = getTriggerCentral();

	vvt_mode_e vvtMode = engineConfiguration->vvtMode[camIndex];
	switch (vvtMode) {
	case VVT_MAP_V_TWIN:
	case VVT_MITSUBISHI_4G63:
	case VVT_MITSUBISHI_4G9x:
		return tc->syncAndReport(crankDivider, 1);
	case VVT_SINGLE_TOOTH:
	case VVT_NISSAN_VQ:
	case VVT_BOSCH_QUICK_START:
	case VVT_MIATA_NB:
	case VVT_MIATA_NA:
	case VVT_TOYOTA_3_TOOTH:
	case VVT_TOYOTA_4_1:
	case VVT_FORD_ST170:
	case VVT_BARRA_3_PLUS_1:
	case VVT_NISSAN_MR:
	case VVT_MAZDA_SKYACTIV:
	case VVT_MAZDA_L:
	case VVT_MITSUBISHI_3A92:
	case VVT_MITSUBISHI_6G75:
	case VVT_HONDA_K_EXHAUST:
		return tc->syncAndReport(crankDivider, 0);
	case VVT_HONDA_K_INTAKE:
	case VVT_INACTIVE:
		// do nothing
		return 0;
	}
	return 0;
}

/**
 * See also wrapAngle
 */
static angle_t wrapVvt(angle_t vvtPosition, int period) {
	// Wrap VVT position in to the range [-360, 360)
	while (vvtPosition < -period / 2) {
		vvtPosition += period;
	}
	while (vvtPosition >= period / 2) {
		vvtPosition -= period;
	}
	return vvtPosition;
}

static angle_t wrapVvtForCamType(angle_t vvtPosition, vvt_mode_e mode) {
	// this could be just an 'if' but let's have it expandable for future use :)
	switch(mode) {
	case VVT_HONDA_K_INTAKE:
		// honda K has four tooth in VVT intake trigger
		// so we just wrap each of those to 720 / 4
	case VVT_TOYOTA_3_TOOTH:
		// Toyota 3 tooth vvt angle comes pre-wrapped,
		// so just ensure the crankOffset didn't break it
		return wrapVvt(vvtPosition, 180);
	default:
		// "normal" cam patterns: wrap in to +-360 deg
		return wrapVvt(vvtPosition, FOUR_STROKE_CYCLE_DURATION);
	}
}

static void logVvtFront(bool isRising, efitick_t nowNt, int index) {
	// If we care about both edges OR displayLogicLevel is set, log every front exactly as it is
	addEngineSnifferVvtEvent(index, isRising);

#if EFI_TOOTH_LOGGER
	LogCamTriggerTooth(nowNt, index, isRising);
#endif /* EFI_TOOTH_LOGGER */
}

void hwHandleVvtCamSignal(bool isRising, efitick_t nowNt, int index) {
	TriggerCentral *tc = getTriggerCentral();
	if (tc->directSelfStimulation || !tc->hwTriggerInputEnabled) {
		// sensor noise + self-stim = loss of trigger sync
		return;
	}

	// Invert if so configured
	isRising ^= engineConfiguration->invertCamVVTSignal;

	logVvtFront(isRising, nowNt, index);

	int bankIndex = index / CAMS_PER_BANK;
	int camIndex = index % CAMS_PER_BANK;
	if (isRising) {
		tc->vvtEventRiseCounter[index]++;
	} else {
		tc->vvtEventFallCounter[index]++;
	}
	if (engineConfiguration->vvtMode[camIndex] == VVT_INACTIVE) {
		warning(ObdCode::CUSTOM_VVT_MODE_NOT_SELECTED, "VVT: event on %d but no mode", camIndex);
	}

	const auto& vvtShape = tc->vvtShape[camIndex];

	bool isVvtWithRealDecoder = vvtWithRealDecoder(engineConfiguration->vvtMode[camIndex]);

	// Non real decoders only use the rising edge
	bool vvtUseOnlyRise = !isVvtWithRealDecoder || vvtShape.useOnlyRisingEdges;
	if (vvtUseOnlyRise && !isRising) {
		// This edge is unimportant, ignore it.
		return;
	}

	// If the main trigger is not synchronized, don't decode VVT yet
	if (!tc->triggerState.getShaftSynchronized()) {
		return;
	}

	TriggerDecoderBase& vvtDecoder = tc->vvtState[bankIndex][camIndex];

	if (isVvtWithRealDecoder) {
		vvtDecoder.decodeTriggerEvent(
				"vvt",
			vvtShape,
			nullptr,
			tc->vvtTriggerConfiguration[camIndex],
			isRising ? TriggerEvent::PrimaryRising : TriggerEvent::PrimaryFalling, nowNt);
	} else {
		vvtDecoder.logEdgeCounters(isRising);
	}

	auto currentTrgPhase = tc->getCurrentEnginePhase(nowNt);
	if (!currentTrgPhase) {
		// If we couldn't resolve engine speed (yet primary trigger is sync'd), this
		// probably means that we have partial crank sync, but not RPM information yet
		return;
	}

	// convert trigger cycle angle into engine cycle angle
	auto currentEnginePhase = tc->toEngPhase(currentTrgPhase.Value);
	// https://github.com/rusefi/rusefi/issues/1713 currentPosition could be negative that's expected

#if EFI_UNIT_TEST
	tc->currentVVTEventPosition[bankIndex][camIndex] = currentEnginePhase.angle;
#endif // EFI_UNIT_TEST

	if (isVvtWithRealDecoder
			&& (!vvtDecoder.getShaftSynchronized() || vvtDecoder.currentCycle.current_index != 0)) {
		// Not in sync or this is not sync tooth - exiting
		return;
	}

	auto vvtPosition = engineConfiguration->vvtOffsets[bankIndex * CAMS_PER_BANK + camIndex] - currentEnginePhase.angle;

	switch(engineConfiguration->vvtMode[camIndex]) {
	case VVT_TOYOTA_3_TOOTH: {
		// Only consider teeth with a sensible VVT angle - this is guaranteed to work so long
		// as the range we check is < 120 deg

		// Wrap to +-180 deg
		vvtPosition = wrapVvt(vvtPosition, 360);

		// Check whether the position is
		// - within range (no resync, just report VVT position)
		// - within range but 360 deg out (engine needs resync)
		bool inRange = vvtPosition > -37 && vvtPosition < 80;

		if (!inRange) {
			return;
		}

		} break;
	default:
		// else, do nothing
		break;
	}

	// Only do engine sync using one cam, other cams just provide VVT position.
	if (index == engineConfiguration->engineSyncCam) {
		bool hadFullSyncBefore = tc->triggerState.hasSynchronizedPhase();
		angle_t crankOffset = adjustCrankPhase(camIndex);
		bool hadFullSyncAfter = tc->triggerState.hasSynchronizedPhase();

		if (!hadFullSyncBefore && hadFullSyncAfter) {
			// vvtPosition was calculated against wrong crank zero position. Now that we have adjusted crank position we
			// shall adjust vvt position as well
			vvtPosition -= crankOffset;
		}

		if (std::abs(currentTrgPhase.Value.angle) < 7) {
			/**
			 * we prefer not to have VVT sync right at trigger sync so that we do not have phase detection error if things happen a bit in
			 * wrong order due to belt flex or else
			 * https://github.com/rusefi/rusefi/issues/3269
			 */
			warning(ObdCode::CUSTOM_VVT_SYNC_POSITION, "VVT sync position too close to trigger sync");
		}
	}

	// Wrap the vvt position back in to the reasonable range
	vvtPosition = wrapVvtForCamType(vvtPosition, engineConfiguration->vvtMode[camIndex]);

	// Only record VVT position if we have full engine sync - may be bogus before that point
	auto& vvtPos = tc->vvtPosition[bankIndex][camIndex];
	if (tc->triggerState.hasSynchronizedPhase()) {
		vvtPos.angle = vvtPosition;
		vvtPos.t.reset(nowNt);
	} else {
		// Reset the timer to long ago to force-invalidate the VVT position
		vvtPos.t.reset(INT64_MIN / 8);
	}
}

int triggerReentrant = 0;
int maxTriggerReentrant = 0;
uint32_t triggerDuration;
uint32_t triggerMaxDuration = 0;

/**
 * This function is called by all "hardaware" trigger inputs:
 *  - Hardware triggers
 *  - Trigger replay from CSV (unit tests)
 */
void hwHandleShaftSignal(int signalIndex, bool isRising, efitick_t timestamp) {
	TriggerCentral *tc = getTriggerCentral();
	ScopePerf perf(PE::HandleShaftSignal);

	if (tc->directSelfStimulation || !tc->hwTriggerInputEnabled) {
		// sensor noise + self-stim = loss of trigger sync
		return;
	}

	handleShaftSignal(signalIndex, isRising, timestamp);
}

// Handle all shaft signals - hardware or emulated both
void handleShaftSignal(int signalIndex, bool isRising, efitick_t timestamp) {
	bool isPrimary = signalIndex == 0;
	if (!isPrimary && !TRIGGER_WAVEFORM(needSecondTriggerInput)) {
		return;
	}

	TriggerEvent signal;
	if (isRising) {
		signal = isPrimary ?
					(engineConfiguration->invertPrimaryTriggerSignal ? TriggerEvent::PrimaryFalling : TriggerEvent::PrimaryRising) :
					(engineConfiguration->invertSecondaryTriggerSignal ? TriggerEvent::SecondaryFalling : TriggerEvent::SecondaryRising);
	} else {
		signal = isPrimary ?
					(engineConfiguration->invertPrimaryTriggerSignal ? TriggerEvent::PrimaryRising : TriggerEvent::PrimaryFalling) :
					(engineConfiguration->invertSecondaryTriggerSignal ? TriggerEvent::SecondaryRising : TriggerEvent::SecondaryFalling);
	}

	// Don't accept trigger input in case of some problems
	if (!getLimpManager()->allowTriggerInput()) {
		return;
	}

#if EFI_TOOTH_LOGGER
	// Log to the Tunerstudio tooth logger
	// We want to do this before anything else as we
	// actually want to capture any noise/jitter that may be occurring

	LogPrimaryTriggerTooth(timestamp, isRising);
#endif /* EFI_TOOTH_LOGGER */

	if (!isUsefulSignal(signal, getTriggerCentral()->triggerShape)) {
		// This is a falling edge in rise-only mode (etc), drop it
		return;
	}

	uint32_t triggerHandlerEntryTime = getTimeNowLowerNt();
	if (triggerReentrant > maxTriggerReentrant)
		maxTriggerReentrant = triggerReentrant;
	triggerReentrant++;

	getTriggerCentral()->handleShaftSignal(signal, timestamp);

	triggerReentrant--;
	triggerDuration = getTimeNowLowerNt() - triggerHandlerEntryTime;
	triggerMaxDuration = maxI(triggerMaxDuration, triggerDuration);
}

void TriggerCentral::resetCounters() {
	triggerState.edgeCountRise = 0;
	triggerState.edgeCountFall = 0;

	for (int i = 0; i < 2; i++) {
		for (int j = 0; j < 2; j++) {
			vvtState[i][j].edgeCountRise = 0;
			vvtState[i][j].edgeCountFall = 0;
		}
	}
}

static const int wheelIndeces[4] = { 0, 0, 1, 1};

static void reportEventToWaveChart(TriggerEvent ckpSignalType, int triggerEventIndex, bool addOppositeEvent) {
	if (!getTriggerCentral()->isEngineSnifferEnabled) { // this is here just as a shortcut so that we avoid engine sniffer as soon as possible
		return; // engineSnifferRpmThreshold is accounted for inside getTriggerCentral()->isEngineSnifferEnabled
	}

	int wheelIndex = wheelIndeces[(int )ckpSignalType];

	bool isUp = isTriggerUpEvent(ckpSignalType);

	addEngineSnifferCrankEvent(wheelIndex, triggerEventIndex, isUp);
	if (addOppositeEvent) {
		// let's add the opposite event right away
		addEngineSnifferCrankEvent(wheelIndex, triggerEventIndex, !isUp);
	}
}

void TriggerCentral::decodeMapCam(efitick_t timestamp, EngPhase currentPhase) {
	if (engineConfiguration->vvtMode[0] == VVT_MAP_V_TWIN &&
			Sensor::getOrZero(SensorType::Rpm) < engineConfiguration->cranking.rpm) {
		// we are trying to figure out which 360 half of the total 720 degree cycle is which, so we compare those in 360 degree sense.
		auto toothAngle360 = currentPhase.angle;
		while (toothAngle360 >= 360) {
			toothAngle360 -= 360;
		}

		if (mapCamPrevToothAngle < engineConfiguration->mapCamDetectionAnglePosition && toothAngle360 > engineConfiguration->mapCamDetectionAnglePosition) {
			// we are somewhere close to 'mapCamDetectionAnglePosition'

			// warning: hack hack hack
			float map = engine->outputChannels.instantMAPValue;

			// Compute diff against the last time we were here
			float diff = map - mapCamPrevCycleValue;
			mapCamPrevCycleValue = map;

			if (diff > 0) {
				mapVvt_map_peak++;
				int revolutionCounter = getTriggerCentral()->triggerState.getCrankSynchronizationCounter();
				mapVvt_MAP_AT_CYCLE_COUNT = revolutionCounter - prevChangeAtCycle;
				prevChangeAtCycle = revolutionCounter;

				hwHandleVvtCamSignal(true,  timestamp, /*index*/0);
				hwHandleVvtCamSignal(false, timestamp, /*index*/0);
#if EFI_UNIT_TEST
				// hack? feature? existing unit test relies on VVT phase available right away
				// but current implementation which is based on periodicFastCallback would only make result available on NEXT tooth
				getLimpManager()->onFastCallback();
#endif // EFI_UNIT_TEST
			}

			mapVvt_MAP_AT_SPECIAL_POINT = map;
			mapVvt_MAP_AT_DIFF = diff;
		}

		mapCamPrevToothAngle = toothAngle360;
	}
}

bool TriggerCentral::isToothExpectedNow(efitick_t timestamp) {
	// Check that the expected next phase (from the last tooth) is close to the actual current phase:
	// basically, check that the tooth width is correct
	auto estimatedCurrentPhase = getCurrentEnginePhase(timestamp);
	auto lastToothPhase = m_lastToothPhaseFromSyncPoint;

	if (expectedNextPhase && estimatedCurrentPhase) {
		float angleError = expectedNextPhase.Value - estimatedCurrentPhase.Value;

		// Wrap around correctly at the end of the cycle
		float cycle = getEngineState()->engineCycle;
		if (angleError < -cycle / 2) {
			angleError += cycle;
		}

		triggerToothAngleError = angleError;

		// Only perform checks if engine is spinning quickly
		// All kinds of garbage happens while cranking
		if (Sensor::getOrZero(SensorType::Rpm) > 1000) {
			// Now compute how close we are to the last tooth decoded
			float angleSinceLastTooth = estimatedCurrentPhase.Value - lastToothPhase;
			if (angleSinceLastTooth < 0.5f) {
				// This tooth came impossibly early, ignore it
				// This rejects things like doubled edges, for example:
				//             |-| |----------------
				//             | | |
				// ____________| |_|
				//             1   2
				//     #1 will be decoded
				//     #2 will be ignored
				// We're not sure which edge was the "real" one, but they were close enough
				// together that it doesn't really matter.
				warning(ObdCode::CUSTOM_PRIMARY_DOUBLED_EDGE, "doubled trigger edge after %.2f deg at #%lu", angleSinceLastTooth, triggerState.currentCycle.current_index);

				return false;
			}

			// Absolute error from last tooth
			float absError = std::abs(angleError);
			float isRpmEnough = Sensor::getOrZero(SensorType::Rpm) > 1000;
			// TODO: configurable threshold
			if (isRpmEnough && absError > 10 && absError < 180) {
				// This tooth came at a very unexpected time, ignore it
				warning(ObdCode::CUSTOM_PRIMARY_BAD_TOOTH_TIMING, "tooth #%lu error of %.1f", triggerState.currentCycle.current_index, angleError);

				// TODO: this causes issues with some real engine logs, should it?
				// return false;
			}
		}
	} else {
		triggerToothAngleError = 0;
	}

	// We aren't ready to reject unexpected teeth, so accept this tooth
	return true;
}

/**
 * This method is NOT invoked for VR falls.
 */
void TriggerCentral::handleShaftSignal(TriggerEvent signal, efitick_t timestamp) {
	if (triggerShape.shapeDefinitionError) {
		// trigger is broken, we cannot do anything here
		warning(ObdCode::CUSTOM_ERR_UNEXPECTED_SHAFT_EVENT, "Shaft event while trigger is mis-configured");
		return;
	}

	if (!isToothExpectedNow(timestamp)) {
		triggerIgnoredToothCount++;
		return;
	}

	isSpinningJustForWatchdog = true;

	// If we've gone a long time without a tooth, reset the tooth skip counter
	if (m_lastEventTimer.hasElapsedSec(1)) {
		m_skipTeethCount = 0;
	}

	m_lastEventTimer.reset(timestamp);

	// If we haven't skipped enough teeth yet, skip this one
	if (m_skipTeethCount < engineConfiguration->triggerSkipPulses) {
		m_skipTeethCount++;
		return;
	}

	// Decode the trigger!
	auto decodeResult = triggerState.decodeTriggerEvent(
			"trigger",
			triggerShape,
			engine,
			primaryTriggerConfiguration,
			signal, timestamp);

	// Don't propagate state if we don't know where we are
	if (!decodeResult) {
		// We don't have sync, but report to the wave chart anyway as index 0.
		reportEventToWaveChart(signal, 0, triggerShape.useOnlyRisingEdges);

		expectedNextPhase = unexpected;

		return;
	}

	ScopePerf perf(PE::ShaftPositionListeners);

	/**
	 * If we only have a crank position sensor with four stroke, here we are extending crank revolutions with a 360 degree
	 * cycle into a four stroke, 720 degrees cycle.
	 */
	int crankDivider = getCrankDivider(triggerShape.getWheelOperationMode());
	int crankInternalIndex = triggerState.getCrankSynchronizationCounter() % crankDivider;
	int triggerIndexForListeners = decodeResult.Value.CurrentIndex + (crankInternalIndex * triggerShape.getSize());

	reportEventToWaveChart(signal, triggerIndexForListeners, triggerShape.useOnlyRisingEdges);

	// Look up this tooth's angle from the sync point. If this tooth is the sync point, we'll get 0 here.
	TrgPhase currentTrgPhase{ getTriggerCentral()->triggerFormDetails.eventAngles[triggerIndexForListeners] };

	// Record precise time and phase of the engine. This is used for VVT decode, and to check that the
	// trigger pattern selected matches reality (ie, we check the next tooth is where we think it should be)
	{
		// under lock to avoid mismatched tooth phase and time
		chibios_rt::CriticalSectionLocker csl;

		m_lastToothTimer.reset(timestamp);
		m_lastToothPhaseFromSyncPoint = currentTrgPhase;
	}

	// Update engine RPM
	rpmShaftPositionCallback(triggerIndexForListeners, timestamp);

	// Schedule the TDC mark
	tdcMarkCallback(triggerIndexForListeners, timestamp);

#if EFI_LOGIC_ANALYZER
	waTriggerEventListener(signal, triggerIndexForListeners, timestamp);
#endif

	EngPhase currentEnginePhase = toEngPhase(currentTrgPhase);
	currentEngineDecodedPhase = currentEnginePhase.angle;

	// TODO: is this logic to compute next trigger tooth angle correct?
	auto nextToothIndex = triggerIndexForListeners;
	TrgPhase nextPhase;

	do {
		// I don't love this.
		nextToothIndex = (nextToothIndex + 1) % engineCycleEventCount;
		nextPhase = { getTriggerCentral()->triggerFormDetails.eventAngles[nextToothIndex] };
		wrapAngle(nextPhase.angle, "nextEnginePhase", ObdCode::CUSTOM_ERR_6555);
	} while (nextPhase == currentTrgPhase);

	expectedNextPhase = nextPhase;

	if (engine->rpmCalculator.getCachedRpm() > 0 && triggerIndexForListeners == 0) {
		engine->module<TpsAccelEnrichment>()->onEngineCycleTps();
	}

	EnginePhaseInfo phaseInfo {
		.timestamp = timestamp,

		.currentTrgPhase = currentTrgPhase,
		.nextTrgPhase = nextPhase,

		.currentEngPhase = currentEnginePhase,
		.nextEngPhase = toEngPhase(nextPhase),
	};

	// Handle ignition and injection
	mainTriggerCallback(triggerIndexForListeners, phaseInfo);

	// Decode the MAP based "cam" sensor
	decodeMapCam(timestamp, currentEnginePhase);
}

static void triggerShapeInfo() {
#if EFI_PROD_CODE || EFI_SIMULATOR
	TriggerWaveform *shape = &getTriggerCentral()->triggerShape;
	TriggerFormDetails *triggerFormDetails = &getTriggerCentral()->triggerFormDetails;
	efiPrintf("syncEdge=%s", getSyncEdge(TRIGGER_WAVEFORM(m_syncEdge)));
	efiPrintf("gap from %.2f to %.2f", TRIGGER_WAVEFORM(syncronizationRatioFrom[0]), TRIGGER_WAVEFORM(syncronizationRatioTo[0]));

	for (size_t i = 0; i < shape->getSize(); i++) {
		efiPrintf("event %d %.2f", i, triggerFormDetails->eventAngles[i]);
	}
#endif
}

#if EFI_PROD_CODE
extern PwmConfig triggerSignal;
#endif /* #if EFI_PROD_CODE */

void triggerInfo(void) {
#if EFI_PROD_CODE || EFI_SIMULATOR

	TriggerCentral *tc = getTriggerCentral();
	TriggerWaveform *ts = &tc->triggerShape;


#if (HAL_TRIGGER_USE_PAL == TRUE) && (PAL_USE_CALLBACKS == TRUE)
		efiPrintf("trigger PAL mode %d", tc->hwTriggerInputEnabled);
#else

#endif /* HAL_TRIGGER_USE_PAL */

	efiPrintf("Template %s (%d) trigger %s (%d) syncEdge=%s tdcOffset=%.2f",
			getEngine_type_e(engineConfiguration->engineType), (int)engineConfiguration->engineType,
			getTrigger_type_e(engineConfiguration->trigger.type), (int)engineConfiguration->trigger.type,
			getSyncEdge(TRIGGER_WAVEFORM(m_syncEdge)), TRIGGER_WAVEFORM(tdcPosition));

	if (engineConfiguration->trigger.type == trigger_type_e::TT_TOOTHED_WHEEL) {
		efiPrintf("total %d/skipped %d", engineConfiguration->trigger.customTotalToothCount,
				engineConfiguration->trigger.customSkippedToothCount);
	}

	efiPrintf("expected cycle events %d/%d",
			TRIGGER_WAVEFORM(getExpectedEventCount(TriggerWheel::T_PRIMARY)),
			TRIGGER_WAVEFORM(getExpectedEventCount(TriggerWheel::T_SECONDARY)));

	efiPrintf("trigger type=%d/need2ndChannel=%s", (int)engineConfiguration->trigger.type,
			boolToString(TRIGGER_WAVEFORM(needSecondTriggerInput)));


	efiPrintf("synchronizationNeeded=%s/isError=%s/total errors=%d ord_err=%lu/total revolutions=%d/self=%s",
			boolToString(ts->isSynchronizationNeeded),
			boolToString(tc->isTriggerDecoderError()),
			tc->triggerState.triggerErrorCounter,
			tc->triggerState.orderingErrorCounter,
			tc->triggerState.getCrankSynchronizationCounter(),
			boolToString(tc->directSelfStimulation));

	if (TRIGGER_WAVEFORM(isSynchronizationNeeded)) {
		efiPrintf("gap from %.2f to %.2f", TRIGGER_WAVEFORM(syncronizationRatioFrom[0]), TRIGGER_WAVEFORM(syncronizationRatioTo[0]));
	}

#endif /* EFI_PROD_CODE || EFI_SIMULATOR */

#if EFI_PROD_CODE

	efiPrintf("primary trigger input: %s", hwPortname(engineConfiguration->triggerInputPins[0]));
	efiPrintf("primary trigger simulator: %s freq=%d",
			hwPortname(engineConfiguration->triggerSimulatorPins[0]),
			engineConfiguration->triggerSimulatorRpm);

	if (ts->needSecondTriggerInput) {
		efiPrintf("secondary trigger input: %s", hwPortname(engineConfiguration->triggerInputPins[1]));
#if EFI_EMULATE_POSITION_SENSORS
		efiPrintf("secondary trigger simulator: %s phase=%d",
				hwPortname(engineConfiguration->triggerSimulatorPins[1]), triggerSignal.safe.phaseIndex);
#endif /* EFI_EMULATE_POSITION_SENSORS */
	}


	for (int camInputIndex = 0; camInputIndex < CAM_INPUTS_COUNT; camInputIndex++) {
		if (isBrainPinValid(engineConfiguration->camInputs[camInputIndex])) {
			int camLogicalIndex = camInputIndex % CAMS_PER_BANK;
			efiPrintf("VVT input: %s mode %s", hwPortname(engineConfiguration->camInputs[camInputIndex]),
					getVvt_mode_e(engineConfiguration->vvtMode[camLogicalIndex]));
			efiPrintf("VVT %d event counters: %d/%d",
					camInputIndex,
					tc->vvtEventRiseCounter[camInputIndex], tc->vvtEventFallCounter[camInputIndex]);
		}
	}

	efiPrintf("primary logic input: %s", hwPortname(engineConfiguration->logicAnalyzerPins[0]));
	efiPrintf("secondary logic input: %s", hwPortname(engineConfiguration->logicAnalyzerPins[1]));


	efiPrintf("totalTriggerHandlerMaxTime=%lu", triggerMaxDuration);

#endif /* EFI_PROD_CODE */

#if EFI_ENGINE_SNIFFER
	efiPrintf("engine sniffer current size=%d", waveChart.getSize());
#endif /* EFI_ENGINE_SNIFFER */

}

static void resetRunningTriggerCounters() {
#if !EFI_UNIT_TEST
	getTriggerCentral()->resetCounters();
	triggerInfo();
#endif
}

void onConfigurationChangeTriggerCallback() {
	static_assert(std::extent<decltype(engine_configuration_s::camInputs)>::value == std::extent<decltype(engine_configuration_s::vvtOffsets)>::value);

	bool changed = false;

	for (size_t camIndex = 0; camIndex < efi::size(engineConfiguration->camInputs); camIndex++) {
		changed |= isConfigurationChanged(camInputs[camIndex]);
		changed |= isConfigurationChanged(vvtOffsets[camIndex]);
	}

	for (size_t i = 0; i < efi::size(engineConfiguration->triggerGapOverrideFrom); i++) {
		changed |= isConfigurationChanged(triggerGapOverrideFrom[i]);
		changed |= isConfigurationChanged(triggerGapOverrideTo[i]);
	}

	for (size_t i = 0; i < efi::size(engineConfiguration->triggerInputPins); i++) {
		changed |= isConfigurationChanged(triggerInputPins[i]);
	}

	for (size_t i = 0; i < efi::size(engineConfiguration->vvtMode); i++) {
		changed |= isConfigurationChanged(vvtMode[i]);
	}

	changed |= isConfigurationChanged(trigger.type);
	changed |= isConfigurationChanged(skippedWheelOnCam);
	changed |= isConfigurationChanged(twoStroke);
	changed |= isConfigurationChanged(globalTriggerAngleOffset);
	changed |= isConfigurationChanged(trigger.customTotalToothCount);
	changed |= isConfigurationChanged(trigger.customSkippedToothCount);
	changed |= isConfigurationChanged(overrideTriggerGaps);

	if (changed) {
	#if EFI_ENGINE_CONTROL
		engine->updateTriggerWaveform();
	#endif
	}
#if EFI_DEFAILED_LOGGING
	efiPrintf("isTriggerConfigChanged=%d", triggerConfigChanged);
#endif /* EFI_DEFAILED_LOGGING */

	// we do not want to miss two updates in a row
	getTriggerCentral()->triggerConfigChangedOnLastConfigurationChange = getTriggerCentral()->triggerConfigChangedOnLastConfigurationChange || changed;
}

static void initVvtShape(TriggerWaveform& shape, const TriggerConfiguration& triggerConfig, TriggerDecoderBase &initState) {
	shape.initializeTriggerWaveform(FOUR_STROKE_CAM_SENSOR, triggerConfig);
	shape.initializeSyncPoint(initState, triggerConfig);
}

void TriggerCentral::validateCamVvtCounters() {
	int camVvtValidationIndex = triggerState.getCrankSynchronizationCounter() % 255;
	if (camVvtValidationIndex == 0) {
		vvtState[0][0].edgeCountRise = 0;
	} else if (camVvtValidationIndex == 0xFE && vvtState[0][0].edgeCountRise < 60) {
		// magic logic: we expect at least 60 CAM/VVT events for each 256 trigger cycles, otherwise throw a code
		warning(ObdCode::OBD_Camshaft_Position_Sensor_Circuit_Range_Performance, "No Camshaft Position Sensor signals");
	}
}
/**
 * Calculate 'shape.triggerShapeSynchPointIndex' value using 'TriggerDecoderBase *state'
 */
static void calculateTriggerSynchPoint(
		const PrimaryTriggerConfiguration &primaryTriggerConfiguration,
		TriggerWaveform& shape,
		TriggerDecoderBase& initState) {

	shape.initializeSyncPoint(initState, primaryTriggerConfiguration);

	if (shape.getSize() >= PWM_PHASE_MAX_COUNT) {
		// todo: by the time we are here we had already modified a lot of RAM out of bounds!
		firmwareError(ObdCode::CUSTOM_ERR_TRIGGER_WAVEFORM_TOO_LONG, "Trigger length above maximum: %d", shape.getSize());
		shape.setShapeDefinitionError(true);
		return;
	}

	if (shape.getSize() == 0) {
		firmwareError(ObdCode::CUSTOM_ERR_TRIGGER_ZERO, "triggerShape size is zero");
	}
}

TriggerDecoderBase initState("init");

void TriggerCentral::updateWaveform() {
	// Re-read config in case it's changed
	primaryTriggerConfiguration.update();
	for (int camIndex = 0; camIndex < CAMS_PER_BANK; camIndex++) {
		vvtTriggerConfiguration[camIndex].update();
	}

	triggerShape.initializeTriggerWaveform(lookupOperationMode(), primaryTriggerConfiguration);

	/**
	 * this is only useful while troubleshooting a new trigger shape in the field
	 * in very VERY rare circumstances
	 */
	if (engineConfiguration->overrideTriggerGaps) {
		int gapIndex = 0;

		// copy however many the user wants
		for (; gapIndex < engineConfiguration->gapTrackingLengthOverride; gapIndex++) {
			float gapOverrideFrom = engineConfiguration->triggerGapOverrideFrom[gapIndex];
			float gapOverrideTo = engineConfiguration->triggerGapOverrideTo[gapIndex];
			TRIGGER_WAVEFORM(setTriggerSynchronizationGap3(/*gapIndex*/gapIndex, gapOverrideFrom, gapOverrideTo));
		}

		// fill the remainder with the default gaps
		for (; gapIndex < GAP_TRACKING_LENGTH; gapIndex++) {
			triggerShape.syncronizationRatioFrom[gapIndex] = NAN;
			triggerShape.syncronizationRatioTo[gapIndex] = NAN;
		}
	}

	if (!triggerShape.shapeDefinitionError) {
		int length = triggerShape.getLength();
		engineCycleEventCount = length;

		efiAssertVoid(ObdCode::CUSTOM_SHAPE_LEN_ZERO, length > 0, "shapeLength=0");

		triggerErrorDetection.clear();

		/**
	 	 * 'initState' instance of TriggerDecoderBase is used only to initialize 'this' TriggerWaveform instance
	 	 * #192 BUG real hardware trigger events could be coming even while we are initializing trigger
	 	 */
		calculateTriggerSynchPoint(primaryTriggerConfiguration,
				triggerShape,
				initState);
	}

	bool primaryNeedsDisambiguation = engine->triggerCentral.triggerShape.needsDisambiguation();

	// If we already don't need disambiguation, we can expect it (immediately)
	bool expectDisambiguation = !primaryNeedsDisambiguation;

	for (int camIndex = 0; camIndex < CAMS_PER_BANK; camIndex++) {
		// todo: should 'vvtWithRealDecoder' be used here?
		if (engineConfiguration->vvtMode[camIndex] != VVT_INACTIVE) {
			initVvtShape(
				vvtShape[camIndex],
				vvtTriggerConfiguration[camIndex],
				initState
			);
		}

		// If we have cam inputs, we can expect trigger disambiguation later.
		// If no cam inputs are set, we can run in crank-only mode
		if (isBrainPinValid(engineConfiguration->camInputs[camIndex])) {
			expectDisambiguation = true;
		}
	}

	// This is not the right place for this, but further refactoring has to happen before it can get moved.
	triggerState.setNeedsDisambiguation(primaryNeedsDisambiguation, expectDisambiguation);
}

/**
 * @returns true if configuration just changed, and if that change has affected trigger
 */
bool TriggerCentral::checkIfTriggerConfigChanged() {
	// we want to make sure that configuration has changed AND that change has changed trigger specifically
	bool result = triggerVersion.isOld(engine->getGlobalConfigurationVersion()) && triggerConfigChangedOnLastConfigurationChange;
	triggerConfigChangedOnLastConfigurationChange = false; // whoever has called the method is supposed to react to changes
	return result;
}

#if EFI_UNIT_TEST
bool TriggerCentral::isTriggerConfigChanged() {
	return triggerConfigChangedOnLastConfigurationChange;
}
#endif // EFI_UNIT_TEST

void validateTriggerInputs() {
	if (!isBrainPinValid(engineConfiguration->triggerInputPins[0]) && isBrainPinValid(engineConfiguration->triggerInputPins[1])) {
		firmwareError(ObdCode::OBD_PCM_Processor_Fault, "First trigger channel is missing");
	}

	if (!isBrainPinValid(engineConfiguration->camInputs[0]) && isBrainPinValid(engineConfiguration->camInputs[2])) {
		firmwareError(ObdCode::OBD_PCM_Processor_Fault, "First bank cam input is required if second bank specified");
	}
}

void initTriggerCentral() {

#if EFI_ENGINE_SNIFFER
	initWaveChart(&waveChart);
#endif /* EFI_ENGINE_SNIFFER */

#if EFI_PROD_CODE || EFI_SIMULATOR
	addConsoleAction(CMD_TRIGGERINFO, triggerInfo);
	addConsoleAction("trigger_shape_info", triggerShapeInfo);
	addConsoleAction("reset_trigger", resetRunningTriggerCounters);
#endif // EFI_PROD_CODE || EFI_SIMULATOR

}

/**
 * @return TRUE is something is wrong with trigger decoding
 */
bool TriggerCentral::isTriggerDecoderError() {
	return triggerErrorDetection.sum(6) > 4;
}

#endif // EFI_SHAFT_POSITION_INPUT
