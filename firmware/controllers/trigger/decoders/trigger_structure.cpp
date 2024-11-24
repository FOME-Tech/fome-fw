/**
 * @file	trigger_structure.cpp
 *
 * @date Jan 20, 2014
 * @author Andrey Belomutskiy, (c) 2012-2020
 *
 * This file is part of rusEfi - see http://rusefi.com
 *
 * rusEfi is free software; you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * rusEfi is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 * even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 */


#include "pch.h"


#include "trigger_chrysler.h"
#include "trigger_gm.h"
#include "trigger_nissan.h"
#include "trigger_mazda.h"
#include "trigger_misc.h"
#include "trigger_mitsubishi.h"
#include "trigger_subaru.h"
#include "trigger_suzuki.h"
#include "trigger_structure.h"
#include "trigger_toyota.h"
#include "trigger_renix.h"
#include "trigger_rover.h"
#include "trigger_honda.h"
#include "trigger_vw.h"
#include "trigger_universal.h"
#include "trigger_mercedes.h"

#if EFI_SENSOR_CHART
#include "sensor_chart.h"
#endif /* EFI_SENSOR_CHART */

TriggerWaveform::TriggerWaveform() {
	initialize(OM_NONE, SyncEdge::Rise);
}

void TriggerWaveform::initialize(operation_mode_e operationMode, SyncEdge syncEdge) {
	isSynchronizationNeeded = true; // that's default value
	isSecondWheelCam = false;
	needSecondTriggerInput = false;
	shapeWithoutTdc = false;

	// If RiseOnly, ignore falling edges completely.
	useOnlyRisingEdges = syncEdge == SyncEdge::RiseOnly;

	setTriggerSynchronizationGap(2);
	for (int gapIndex = 1; gapIndex < GAP_TRACKING_LENGTH ; gapIndex++) {
		// NaN means do not use this gap ratio
		setTriggerSynchronizationGap3(gapIndex, NAN, 100000);
	}
	gapTrackingLength = 1;

	tdcPosition = 0;
	shapeDefinitionError = false;
	useOnlyPrimaryForSync = false;

	m_operationMode = operationMode;
	m_syncEdge = syncEdge;
	triggerShapeSynchPointIndex = 0;
	memset(expectedEventCount, 0, sizeof(expectedEventCount));
	wave.reset();
	wave.waveCount = TRIGGER_INPUT_PIN_COUNT;
	wave.phaseCount = 0;
	previousAngle = 0;
	memset(isRiseEvent, 0, sizeof(isRiseEvent));
#if EFI_UNIT_TEST
	memset(&triggerSignalIndeces, 0, sizeof(triggerSignalIndeces));
	memset(&triggerSignalStates, 0, sizeof(triggerSignalStates));
	knownOperationMode = true;
#endif // EFI_UNIT_TEST
}

size_t TriggerWaveform::getSize() const {
	return wave.phaseCount;
}

int TriggerWaveform::getTriggerWaveformSynchPointIndex() const {
	return triggerShapeSynchPointIndex;
}

/**
 * physical primary trigger duration
 * @see getEngineCycle
 * @see getCrankDivider
 */
angle_t TriggerWaveform::getCycleDuration() const {
	switch (m_operationMode) {
	case FOUR_STROKE_THREE_TIMES_CRANK_SENSOR:
		return FOUR_STROKE_CYCLE_DURATION / SYMMETRICAL_THREE_TIMES_CRANK_SENSOR_DIVIDER;
	case FOUR_STROKE_SYMMETRICAL_CRANK_SENSOR:
		return FOUR_STROKE_CYCLE_DURATION / SYMMETRICAL_CRANK_SENSOR_DIVIDER;
	case FOUR_STROKE_TWELVE_TIMES_CRANK_SENSOR:
		return FOUR_STROKE_CYCLE_DURATION / SYMMETRICAL_TWELVE_TIMES_CRANK_SENSOR_DIVIDER;
	case FOUR_STROKE_CRANK_SENSOR:
	case TWO_STROKE:
		return TWO_STROKE_CYCLE_DURATION;
	default:
		return FOUR_STROKE_CYCLE_DURATION;
	}
}

bool TriggerWaveform::needsDisambiguation() const {
	switch (getWheelOperationMode()) {
		case FOUR_STROKE_CRANK_SENSOR:
		case FOUR_STROKE_SYMMETRICAL_CRANK_SENSOR:
		case FOUR_STROKE_THREE_TIMES_CRANK_SENSOR:
		case FOUR_STROKE_TWELVE_TIMES_CRANK_SENSOR:
			return true;
		case FOUR_STROKE_CAM_SENSOR:
		case TWO_STROKE:
			return false;
		default:
			firmwareError(ObdCode::OBD_PCM_Processor_Fault, "bad operationMode() in needsDisambiguation");
			return true;
	}
}

/**
 * Trigger event count equals engine cycle event count if we have a cam sensor.
 * Two trigger cycles make one engine cycle in case of a four stroke engine If we only have a cranksensor.
 *
 * 'engine->engineCycleEventCount' hold a pre-calculated copy of this value as a performance optimization
 */
size_t TriggerWaveform::getLength() const {
	/**
	 * 24 for FOUR_STROKE_TWELVE_TIMES_CRANK_SENSOR
	 * 6 for FOUR_STROKE_THREE_TIMES_CRANK_SENSOR
	 * 4 for FOUR_STROKE_SYMMETRICAL_CRANK_SENSOR
	 * 2 for FOUR_STROKE_CRANK_SENSOR
	 * 1 otherwise
	 */
	int multiplier = getEngineCycle(m_operationMode) / getCycleDuration();
	return multiplier * getSize();
}

angle_t TriggerWaveform::getAngle(int index) const {
	/**
	 * FOUR_STROKE_CRANK_SENSOR magic:
	 * We have two crank shaft revolutions for each engine cycle
	 * See also trigger_central.cpp
	 * See also getEngineCycleEventCount()
	 */
	efiAssert(ObdCode::CUSTOM_ERR_ASSERT, wave.phaseCount != 0, "shapeSize=0", NAN);
	int crankCycle = index / wave.phaseCount;
	int remainder = index % wave.phaseCount;

	auto cycleStartAngle = getCycleDuration() * crankCycle;
	auto positionWithinCycle = getSwitchAngle(remainder);

	return cycleStartAngle + positionWithinCycle;
}

void TriggerWaveform::addEventClamped(angle_t angle, bool stateParam, TriggerWheel const channelIndex, float filterLeft, float filterRight) {
	if (angle > filterLeft && angle < filterRight) {
		addEvent(angle / getEngineCycle(m_operationMode), stateParam, channelIndex);
	}
}

/**
 * See also Engine#getOperationMode which accounts for additional settings which are
 * needed to resolve precise mode for vague wheels
 */
operation_mode_e TriggerWaveform::getWheelOperationMode() const {
	return m_operationMode;
}

#if EFI_UNIT_TEST
extern bool printTriggerDebug;
#endif

size_t TriggerWaveform::getExpectedEventCount(TriggerWheel channelIndex) const {
	return expectedEventCount[(int)channelIndex];
}

void TriggerWaveform::calculateExpectedEventCounts() {
	if (!useOnlyRisingEdges) {
		for (size_t i = 0; i < efi::size(expectedEventCount); i++) {
			if (getExpectedEventCount((TriggerWheel)i) % 2 != 0) {
				firmwareError(ObdCode::ERROR_TRIGGER_DRAMA, "Trigger: should be even number of events index=%d count=%d", i, getExpectedEventCount((TriggerWheel)i));
			}
		}
	}

	bool isSingleToothOnPrimaryChannel = useOnlyRisingEdges ? getExpectedEventCount(TriggerWheel::T_PRIMARY) == 1 : getExpectedEventCount(TriggerWheel::T_PRIMARY) == 2;
	// todo: next step would be to set 'isSynchronizationNeeded' automatically based on the logic we have here
	if (!shapeWithoutTdc && isSingleToothOnPrimaryChannel != !isSynchronizationNeeded) {
		firmwareError(ObdCode::ERROR_TRIGGER_DRAMA, "shapeWithoutTdc isSynchronizationNeeded isSingleToothOnPrimaryChannel constraint violation");
	}
	if (isSingleToothOnPrimaryChannel) {
		useOnlyPrimaryForSync = true;
	} else {
		if (getExpectedEventCount(TriggerWheel::T_SECONDARY) == 0 && useOnlyPrimaryForSync) {
			firmwareError(ObdCode::ERROR_TRIGGER_DRAMA, "why would you set useOnlyPrimaryForSync with only one trigger wheel?");
		}
	}
}

void TriggerWaveform::addEvent720(angle_t angle, bool state, TriggerWheel const channelIndex) {
	addEvent(angle / FOUR_STROKE_CYCLE_DURATION, state, channelIndex);
}

void TriggerWaveform::addEvent360(angle_t angle, bool state, TriggerWheel const channelIndex) {
	efiAssertVoid(ObdCode::CUSTOM_OMODE_UNDEF, m_operationMode == FOUR_STROKE_CAM_SENSOR || m_operationMode == FOUR_STROKE_CRANK_SENSOR, "Not a mode for 360");
	addEvent(CRANK_MODE_MULTIPLIER * angle / FOUR_STROKE_CYCLE_DURATION, state, channelIndex);
}

void TriggerWaveform::addEventAngle(angle_t angle, bool state, TriggerWheel const channelIndex) {
	addEvent(angle / getCycleDuration(), state, channelIndex);
}

void TriggerWaveform::addEvent(angle_t angle, bool state, TriggerWheel const channelIndex) {
	efiAssertVoid(ObdCode::CUSTOM_OMODE_UNDEF, m_operationMode != OM_NONE, "operationMode not set");

	if (channelIndex == TriggerWheel:: T_SECONDARY) {
		needSecondTriggerInput = true;
	}

#if EFI_UNIT_TEST
	if (printTriggerDebug) {
		printf("addEvent2 %.2f i=%d r/f=%d\r\n", angle, channelIndex, state);
	}
#endif

#if EFI_UNIT_TEST
	assertIsInBounds(wave.phaseCount, triggerSignalIndeces, "trigger shape overflow");
	triggerSignalIndeces[wave.phaseCount] = channelIndex;
	triggerSignalStates[wave.phaseCount] = state;
#endif // EFI_UNIT_TEST


	// todo: the whole 'useOnlyRisingEdgeForTrigger' parameter and logic should not be here
	// todo: see calculateExpectedEventCounts
	// related calculation should be done once trigger is initialized outside of trigger shape scope
	if (!useOnlyRisingEdges || state) {
		expectedEventCount[(int)channelIndex]++;
	}

	if (angle <= 0 || angle > 1) {
		firmwareError(ObdCode::CUSTOM_ERR_6599, "angle should be positive not above 1: index=%d angle %f", (int)channelIndex, angle);
		return;
	}
	if (wave.phaseCount > 0) {
		if (angle <= previousAngle) {
			warning(ObdCode::CUSTOM_ERR_TRG_ANGLE_ORDER, "invalid angle order: new=%.2f/%f and prev=%.2f/%f, size=%d",
					angle, angle * getCycleDuration(),
					previousAngle, previousAngle * getCycleDuration(),
					wave.phaseCount);
			setShapeDefinitionError(true);
			return;
		}
	}
	previousAngle = angle;
	if (wave.phaseCount == 0) {
		wave.phaseCount = 1;
		for (int i = 0; i < PWM_PHASE_MAX_WAVE_PER_PWM; i++) {
			wave.setChannelState(i, /* switchIndex */ 0, /* value */ false);
		}

		isRiseEvent[0] = state;
		wave.setSwitchTime(0, angle);
		wave.setChannelState((int)channelIndex, /* channelIndex */ 0, /* value */ state);
		return;
	}

	if (wave.findAngleMatch(angle)) {
		warning(ObdCode::CUSTOM_ERR_SAME_ANGLE, "same angle: not supported");
		setShapeDefinitionError(true);
		return;
	}

	int index = wave.findInsertionAngle(angle);

	/**
	 * todo: it would be nice to be able to provide trigger angles without sorting them externally
	 * The idea here is to shift existing data - including handling high vs low state of the signals
	 */
	// todo: does this logic actually work? I think it does not! due to broken state handling
/*
	for (int i = size - 1; i >= index; i--) {
		for (int j = 0; j < PWM_PHASE_MAX_WAVE_PER_PWM; j++) {
			wave.waves[j].pinStates[i + 1] = wave.getChannelState(j, index);
		}
		wave.setSwitchTime(i + 1, wave.getSwitchTime(i));
	}
*/
	isRiseEvent[index] = state;

	if ((unsigned)index != wave.phaseCount) {
		firmwareError(ObdCode::ERROR_TRIGGER_DRAMA, "are we ever here?");
	}

	wave.phaseCount++;

	for (int i = 0; i < PWM_PHASE_MAX_WAVE_PER_PWM; i++) {
		bool value = wave.getChannelState(/* channelIndex */i, index - 1);
		wave.setChannelState(i, index, value);
	}
	wave.setSwitchTime(index, angle);
	wave.setChannelState((int)channelIndex, index, state);
}

angle_t TriggerWaveform::getSwitchAngle(int index) const {
	return getCycleDuration() * wave.getSwitchTime(index);
}

void TriggerWaveform::setTriggerSynchronizationGap2(float syncRatioFrom, float syncRatioTo) {
	setTriggerSynchronizationGap3(/*gapIndex*/0, syncRatioFrom, syncRatioTo);
}

void TriggerWaveform::setTriggerSynchronizationGap3(int gapIndex, float syncRatioFrom, float syncRatioTo) {
	isSynchronizationNeeded = true;
	efiAssertVoid(ObdCode::OBD_PCM_Processor_Fault, gapIndex >= 0 && gapIndex < GAP_TRACKING_LENGTH, "gapIndex out of range");
	syncronizationRatioFrom[gapIndex] = syncRatioFrom;
	syncronizationRatioTo[gapIndex] = syncRatioTo;
	if (gapIndex == 0) {
		// we have a special case here - only sync with one gap has this feature
		this->syncRatioAvg = (int)efiRound((syncRatioFrom + syncRatioTo) * 0.5f, 1.0f);
	}
	gapTrackingLength = maxI(1 + gapIndex, gapTrackingLength);

#if EFI_UNIT_TEST
	if (printTriggerDebug) {
		printf("setTriggerSynchronizationGap3 %d %.2f %.2f\r\n", gapIndex, syncRatioFrom, syncRatioTo);
	}
#endif /* EFI_UNIT_TEST */

}

uint16_t TriggerWaveform::findAngleIndex(TriggerFormDetails *details, angle_t targetAngle) const {
	size_t engineCycleEventCount = getLength();

	efiAssert(ObdCode::CUSTOM_ERR_ASSERT, engineCycleEventCount != 0 && engineCycleEventCount <= 0xFFFF,
		  "engineCycleEventCount", 0);

	uint32_t left = 0;
	uint32_t right = engineCycleEventCount - 1;

	/**
	 * Let's find the last trigger angle which is less or equal to the desired angle
	 * todo: extract binary search as template method?
	 */
	do {
		uint32_t middle = (left + right) / 2;

		if (details->eventAngles[middle] <= targetAngle) {
			left = middle + 1;
		} else {
			right = middle - 1;
		}
	} while (left <= right);
	left -= 1;
	if (useOnlyRisingEdges) {
		left &= ~1U;
	}
	return left;
}

void TriggerWaveform::setShapeDefinitionError(bool value) {
	shapeDefinitionError = value;
}

void TriggerWaveform::setTriggerSynchronizationGap(float syncRatio) {
	setTriggerSynchronizationGap3(/*gapIndex*/0, syncRatio * TRIGGER_GAP_DEVIATION_LOW, syncRatio * TRIGGER_GAP_DEVIATION_HIGH);
}

void TriggerWaveform::setSecondTriggerSynchronizationGap(float syncRatio) {
	setTriggerSynchronizationGap3(/*gapIndex*/1, syncRatio * TRIGGER_GAP_DEVIATION_LOW, syncRatio * TRIGGER_GAP_DEVIATION_HIGH);
}

void TriggerWaveform::setSecondTriggerSynchronizationGap2(float syncRatioFrom, float syncRatioTo) {
	setTriggerSynchronizationGap3(/*gapIndex*/1, syncRatioFrom, syncRatioTo);
}

void TriggerWaveform::setThirdTriggerSynchronizationGap(float syncRatio) {
	setTriggerSynchronizationGap3(/*gapIndex*/2, syncRatio * TRIGGER_GAP_DEVIATION_LOW, syncRatio * TRIGGER_GAP_DEVIATION_HIGH);
}

/**
 * External logger is needed because at this point our logger is not yet initialized
 */
void TriggerWaveform::initializeTriggerWaveform(operation_mode_e triggerOperationMode, const TriggerConfiguration& triggerConfig) {

#if EFI_PROD_CODE
	efiAssertVoid(ObdCode::CUSTOM_ERR_6641, getCurrentRemainingStack() > EXPECTED_REMAINING_STACK, "init t");
	efiPrintf("initializeTriggerWaveform(%s/%d)", getTrigger_type_e(triggerConfig.TriggerType.type), (int)triggerConfig.TriggerType.type);
#endif

	shapeDefinitionError = false;

	switch (triggerConfig.TriggerType.type) {
	case trigger_type_e::TT_TOOTHED_WHEEL:
		initializeSkippedToothTrigger(this, triggerConfig.TriggerType.customTotalToothCount,
				triggerConfig.TriggerType.customSkippedToothCount, triggerOperationMode, SyncEdge::RiseOnly);
		break;

	case trigger_type_e::TT_MAZDA_MIATA_NA:
		initializeMazdaMiataNaShape(this);
		break;

	case trigger_type_e::TT_MITSUBISHI_MESS:
		initializeMitsubishi4gMess(this);
		break;

	case trigger_type_e::TT_MAZDA_MIATA_VVT_TEST:
		initializeMazdaMiataVVtTestShape(this);
		break;

	case trigger_type_e::TT_SUZUKI_G13B:
		initializeSuzukiG13B(this);
		break;

	case trigger_type_e::TT_FORD_TFI_PIP:
		configureFordPip(this);
		break;

	case trigger_type_e::TT_FORD_ST170:
		configureFordST170(this);
		break;

	case trigger_type_e::TT_VVT_MIATA_NB:
		initializeMazdaMiataVVtCamShape(this);
		break;

	case trigger_type_e::TT_VVT_MIATA_NA:
		initializeMazdaMiataNaCamShape(this);
		break;

	case trigger_type_e::TT_RENIX_66_2_2_2:
		initializeRenix66_2_2(this);
		break;

	case trigger_type_e::TT_RENIX_44_2_2:
		initializeRenix44_2_2(this);
		break;

	case trigger_type_e::TT_MIATA_VVT:
		initializeMazdaMiataNb2Crank(this);
		break;

	case trigger_type_e::TT_DODGE_NEON_1995:
	case trigger_type_e::TT_DODGE_NEON_1995_ONLY_CRANK:
		configureNeon1995TriggerWaveformOnlyCrank(this);
		break;

	case trigger_type_e::TT_DODGE_STRATUS:
		configureDodgeStratusTriggerWaveform(this);
		break;

	case trigger_type_e::TT_DODGE_NEON_2003_CAM:
		configureNeon2003TriggerWaveformCam(this);
		break;

	case trigger_type_e::TT_DODGE_NEON_2003_CRANK:
		configureNeon2003TriggerWaveformCam(this);
//		configureNeon2003TriggerWaveformCrank(triggerShape);
		break;

	case trigger_type_e::TT_FORD_ASPIRE:
		configureFordAspireTriggerWaveform(this);
		break;

	case trigger_type_e::TT_VVT_NISSAN_VQ35:
		initializeNissanVQvvt(this);
		break;

	case trigger_type_e::TT_VVT_MITSUBISHI_3A92:
		initializeVvt3A92(this);
		break;

	case trigger_type_e::TT_VVT_TOYOTA_4_1:
		initializeSkippedToothTrigger(this, 4, 1, triggerOperationMode, SyncEdge::RiseOnly);
		setTriggerSynchronizationGap3(/*gapIndex*/0, /*from*/1.60, 2.40);
		setTriggerSynchronizationGap3(/*gapIndex*/1, /*from*/0.75, 1.25);
		break;

	case trigger_type_e::TT_VVT_MITSUBISHI_6G75:
	case trigger_type_e::TT_NISSAN_QR25:
		initializeNissanQR25crank(this);
		break;

	case trigger_type_e::TT_NISSAN_VQ30:
		initializeNissanVQ30cam(this);
		break;

	case trigger_type_e::TT_NISSAN_VQ35:
		initializeNissanVQ35crank(this);
		break;

	case trigger_type_e::TT_NISSAN_MR18_CRANK:
		initializeNissanMR18crank(this);
		break;

	case trigger_type_e::TT_NISSAN_MR18_CAM_VVT:
		initializeNissanMRvvt(this);
		break;

	case trigger_type_e::TT_KAWA_KX450F:
		configureKawaKX450F(this);
		break;

	case trigger_type_e::TT_SKODA_FAVORIT:
		setSkodaFavorit(this);
		break;

	case trigger_type_e::TT_GM_60_2_2_2:
		configureGm60_2_2_2(this);
		break;

	case trigger_type_e::TT_GM_7X:
		configureGmTriggerWaveform(this);
		break;

	case trigger_type_e::TT_MAZDA_DOHC_1_4:
		configureMazdaProtegeLx(this);
		break;

	case trigger_type_e::TT_ONE_PLUS_ONE:
		configureOnePlusOne(this);
		break;

	case trigger_type_e::TT_3_1_CAM:
		configure3_1_cam(this);
		break;

	case trigger_type_e::TT_MERCEDES_2_SEGMENT:
		setMercedesTwoSegment(this);
		break;

	case trigger_type_e::TT_ONE:
		initializeSkippedToothTrigger(this, 1, 0, triggerOperationMode, SyncEdge::Rise);
		break;

	case trigger_type_e::TT_MAZDA_SOHC_4:
		configureMazdaProtegeSOHC(this);
		break;

	case trigger_type_e::TT_DAIHATSU:
		configureDaihatsu4(this);
		break;

	case trigger_type_e::TT_VVT_TOYOTA_3_TOOTH:
		initializeSkippedToothTrigger(this, 3, 0, triggerOperationMode, SyncEdge::RiseOnly);
		break;

	case trigger_type_e::TT_36_2_1_1:
		initialize36_2_1_1(this);
		break;

	case trigger_type_e::TT_36_2_1:
		initialize36_2_1(this);
		break;

	case trigger_type_e::TT_TOOTHED_WHEEL_32_2:
		initializeSkippedToothTrigger(this, 32, 2, triggerOperationMode, SyncEdge::RiseOnly);
		// todo: why is this 32/2 asking for third gap while 60/2 is happy with just two gaps?
		// method above sets second gap, here we add third
		// this third gap is not required to sync on perfect signal but is needed to handle to reject cranking transition noise
		setThirdTriggerSynchronizationGap(1);
		break;

	case trigger_type_e::TT_TOOTHED_WHEEL_60_2:
		initializeSkippedToothTrigger(this, 60, 2, triggerOperationMode, SyncEdge::RiseOnly);
		break;

	case trigger_type_e::TT_TOOTHED_WHEEL_36_2:
		initializeSkippedToothTrigger(this, 36, 2, triggerOperationMode, SyncEdge::RiseOnly);
		setTriggerSynchronizationGap3(/*gapIndex*/0, /*from*/1.6, 3.5);
		setTriggerSynchronizationGap3(/*gapIndex*/1, /*from*/0.7, 1.3); // second gap is not required to synch on perfect signal but is needed to handle to reject cranking transition noise
		break;

	case trigger_type_e::TT_60_2_VW:
		setVwConfiguration(this);
		break;

	case trigger_type_e::TT_TOOTHED_WHEEL_36_1:
		initializeSkippedToothTrigger(this, 36, 1, triggerOperationMode, SyncEdge::RiseOnly);
		break;

	case trigger_type_e::TT_VVT_BOSCH_QUICK_START:
		configureQuickStartSenderWheel(this);
		break;

	case trigger_type_e::TT_VVT_BARRA_3_PLUS_1:
		configureBarra3plus1cam(this);
		break;

	case trigger_type_e::TT_HONDA_K_CAM_4_1:
		configureHondaK_4_1(this);
		break;

	case trigger_type_e::TT_HONDA_K_CRANK_12_1:
		configureHondaK_12_1(this);
		break;

	case trigger_type_e::TT_SUBARU_EZ30:
		initializeSubaruEZ30(this);
		break;

	case trigger_type_e::TT_VVT_MAZDA_SKYACTIV:
		initializeMazdaSkyactivCam(this);
		break;

	case trigger_type_e::TT_VVT_MAZDA_L:
		initializeMazdaLCam(this);
		break;

	case trigger_type_e::TT_BENELLI_TRE:
		configureBenelli(this);
		break;

	case trigger_type_e::TT_MITSU_4G63_CAM:
		initializeMitsubishi4g63Cam(this);
		break;
	case trigger_type_e::TT_MITSU_4G9x_CAM:
		initializeMitsubishi4g9xCam(this);
		break;
	case trigger_type_e::TT_1_16:
		configureOnePlus16(this);
		break;

	case trigger_type_e::TT_HONDA_CBR_600:
		configureHondaCbr600(this);
		break;

	case trigger_type_e::TT_CHRYSLER_NGC_36_2_2:
		configureChryslerNGC_36_2_2(this);
		break;

	case trigger_type_e::TT_MITSUBISHI_4G93:
		initializeMitsubishi4G93(this);
		break;

	case trigger_type_e::TT_DODGE_RAM:
		initDodgeRam(this);
		break;

	case trigger_type_e::TT_JEEP_4_CYL:
		initJeep_XJ_4cyl_2500(this);
		break;

	case trigger_type_e::TT_JEEP_18_2_2_2:
		initJeep18_2_2_2(this);
		break;

	case trigger_type_e::TT_SUBARU_7_6:
		initializeSubaru7_6(this);
		break;

	case trigger_type_e::TT_36_2_2_2:
		initialize36_2_2_2(this);
		break;

	case trigger_type_e::TT_2JZ_3_34:
		initialize2jzGE3_34_simulation_shape(this);
		break;

	case trigger_type_e::TT_12_TOOTH_CRANK:
		configure12ToothCrank(this);
		break;

	case trigger_type_e::TT_NISSAN_SR20VE:
		initializeNissanSR20VE_4(this);
		break;

	case trigger_type_e::TT_ROVER_K:
		initializeRoverK(this);
		break;

	case trigger_type_e::TT_FIAT_IAW_P8:
		configureFiatIAQ_P8(this);
		break;

	case trigger_type_e::TT_TRI_TACH:
		configureTriTach(this);
		break;

	case trigger_type_e::TT_GM_24x:
		initGmLS24_5deg(this);
		break;

	case trigger_type_e::TT_GM_24x_2:
		initGmLS24_3deg(this);
		break;

	case trigger_type_e::TT_SUBARU_7_WITHOUT_6:
		initializeSubaruOnly7(this);
		break;

	case trigger_type_e::TT_SUBARU_SVX:
		initializeSubaru_SVX(this);
		break;

	case trigger_type_e::TT_SUBARU_SVX_CRANK_1:
		initializeSubaru_SVX(this);
		break;

	case trigger_type_e::TT_SUBARU_SVX_CAM_VVT:
		initializeSubaru_SVX(this);
		break;

	default:
		setShapeDefinitionError(true);
		warning(ObdCode::CUSTOM_ERR_NO_SHAPE, "initializeTriggerWaveform() not implemented: %d", (int)triggerConfig.TriggerType.type);
	}

	/**
	 * Feb 2019 suggestion: it would be an improvement to remove 'expectedEventCount' logic from 'addEvent'
	 * and move it here, after all events were added.
	 */
	calculateExpectedEventCounts();
	version++;

	if (!shapeDefinitionError) {
		wave.checkSwitchTimes(getCycleDuration());
	}
}
