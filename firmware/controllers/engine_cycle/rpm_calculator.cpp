/**
 * @file    rpm_calculator.cpp
 * @brief   RPM calculator
 *
 * Here we listen to position sensor events in order to figure our if engine is currently running or not.
 * Actual getRpm() is calculated once per crankshaft revolution, based on the amount of time passed
 * since the start of previous shaft revolution.
 *
 * We also have 'instant RPM' logic separate from this 'cycle RPM' logic. Open question is why do we not use
 * instant RPM instead of cycle RPM more often.
 *
 * @date Jan 1, 2013
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"

#include "trigger_central.h"

#include "engine_sniffer.h"

// See RpmCalculator::checkIfSpinning()
#ifndef NO_RPM_EVENTS_TIMEOUT_SECS
#define NO_RPM_EVENTS_TIMEOUT_SECS 2
#endif /* NO_RPM_EVENTS_TIMEOUT_SECS */

float RpmCalculator::getRpmAcceleration() const {
	return rpmRate;
}

bool RpmCalculator::isStopped() const {
	// Spinning-up with zero RPM means that the engine is not ready yet, and is treated as 'stopped'.
	return state == STOPPED || (state == SPINNING_UP && cachedRpmValue == 0);
}

bool RpmCalculator::isCranking() const {
	// Spinning-up with non-zero RPM is suitable for all engine math, as good as cranking
	return state == CRANKING || (state == SPINNING_UP && cachedRpmValue > 0);
}

bool RpmCalculator::isSpinningUp() const {
	return state == SPINNING_UP;
}

uint32_t RpmCalculator::getRevolutionCounterSinceStart(void) const {
	return revolutionCounterSinceStart;
}

/**
 * @return -1 in case of isNoisySignal(), current RPM otherwise
 * See NOISY_RPM
 */
float RpmCalculator::getCachedRpm() const {
	return cachedRpmValue;
}

operation_mode_e lookupOperationMode() {
	if (engineConfiguration->twoStroke) {
		return TWO_STROKE;
	} else {
		return engineConfiguration->skippedWheelOnCam ? FOUR_STROKE_CAM_SENSOR : FOUR_STROKE_CRANK_SENSOR;
	}
}

// see also in TunerStudio project '[doesTriggerImplyOperationMode] tag
// this is related to 'knownOperationMode' flag
static bool doesTriggerImplyOperationMode(trigger_type_e type) {
	switch (type) {
		case trigger_type_e::TT_TOOTHED_WHEEL:
		case trigger_type_e::TT_ONE:
		case trigger_type_e::TT_3_1_CAM:
		case trigger_type_e::TT_36_2_2_2:	// TODO: should this one be in this list?
		case trigger_type_e::TT_TOOTHED_WHEEL_60_2:
		case trigger_type_e::TT_TOOTHED_WHEEL_36_1:
			// These modes could be either cam or crank speed
			return false;
		default:
			return true;
	}
}

// todo: move to triggerCentral/triggerShape since has nothing to do with rotation state!
operation_mode_e RpmCalculator::getOperationMode() const {
#if EFI_SHAFT_POSITION_INPUT
	// Ignore user-provided setting for well known triggers.
	if (doesTriggerImplyOperationMode(engineConfiguration->trigger.type)) {
		// For example for Miata NA, there is no reason to allow user to set FOUR_STROKE_CRANK_SENSOR
		return engine->triggerCentral.triggerShape.getWheelOperationMode();
	} else
#endif // EFI_SHAFT_POSITION_INPUT
	{
		// For example 36-1, could be on either cam or crank, so we have to ask the user
		return lookupOperationMode();
	}
}


#if EFI_SHAFT_POSITION_INPUT

RpmCalculator::RpmCalculator() :
		StoredValueSensor(SensorType::Rpm, efidur_t::zero())
	{
	assignRpmValue(0);
}

/**
 * @return true if there was a full shaft revolution within the last second
 */
bool RpmCalculator::isRunning() const {
	return state == RUNNING;
}

/**
 * @return true if engine is spinning (cranking or running)
 */
bool RpmCalculator::checkIfSpinning(efitick_t nowNt) const {
	if (getLimpManager()->shutdownController.isEngineStop(nowNt)) {
		return false;
	}

	// Anything below 60 rpm is not running
	bool noRpmEventsForTooLong = lastTdcTimer.getElapsedSeconds(nowNt) > NO_RPM_EVENTS_TIMEOUT_SECS;

	/**
	 * Also check if there were no trigger events
	 */
	bool noTriggerEventsForTooLong = !engine->triggerCentral.engineMovedRecently(nowNt);

	if (noRpmEventsForTooLong || noTriggerEventsForTooLong) {
		return false;
	}

	return true;
}

void RpmCalculator::assignRpmValue(float floatRpmValue) {
	previousRpmValue = cachedRpmValue;

	cachedRpmValue = floatRpmValue;

	setValidValue(floatRpmValue, {});	// 0 for current time since RPM sensor never times out
	if (cachedRpmValue <= 0) {
		oneDegreeUs = NAN;
	} else {
		// here it's really important to have more precise float RPM value, see #796
		oneDegreeUs = getOneDegreeTimeUs(floatRpmValue);
		if (previousRpmValue == 0) {
			/**
			 * this would make sure that we have good numbers for first cranking revolution
			 * #275 cranking could be improved
			 */
			engine->periodicFastCallback();
		}
	}
}

void RpmCalculator::setRpmValue(float value) {
	assignRpmValue(value);

	// Change state
	if (cachedRpmValue == 0) {
		state = STOPPED;
	} else if (cachedRpmValue >= engineConfiguration->cranking.rpm) {
		if (state != RUNNING) {
			// Store the time the engine started
			engineStartTimer.reset();
		}

		state = RUNNING;
	} else if (state == STOPPED || state == SPINNING_UP) {
		/**
		 * We are here if RPM is above zero but we have not seen running RPM yet.
		 * This gives us cranking hysteresis - a drop of RPM during running is still running, not cranking.
		 */
		state = CRANKING;
	}
}

spinning_state_e RpmCalculator::getState() const {
	return state;
}

void RpmCalculator::onNewEngineCycle() {
	revolutionCounterSinceBoot++;
	revolutionCounterSinceStart++;
}

uint32_t RpmCalculator::getRevolutionCounterM(void) const {
	return revolutionCounterSinceBoot;
}

void RpmCalculator::onSlowCallback() {
	// Stop the engine if it's been too long since we got a trigger event
	if (!engine->triggerCentral.engineMovedRecently(getTimeNowNt())) {
		setStopSpinning();
	}
}

void RpmCalculator::setStopped() {
	revolutionCounterSinceStart = 0;

	rpmRate = 0;

	if (cachedRpmValue != 0) {
		assignRpmValue(0);
		efiPrintf("engine stopped");
	}
	state = STOPPED;
}

void RpmCalculator::setStopSpinning() {
	isSpinning = false;
	setStopped();
}

void RpmCalculator::setSpinningUp(efitick_t nowNt) {
	if (!engineConfiguration->isFasterEngineSpinUpEnabled)
		return;
	// Only a completely stopped and non-spinning engine can enter the spinning-up state.
	if (isStopped() && !isSpinning) {
		state = SPINNING_UP;
		engine->triggerCentral.instantRpm.spinningEventIndex = 0;
		isSpinning = true;
	}
	// update variables needed by early instant RPM calc.
	if (isSpinningUp() && !engine->triggerCentral.triggerState.getShaftSynchronized()) {
		engine->triggerCentral.instantRpm.setLastEventTimeForInstantRpm(nowNt);
	}
}

/**
 * @brief Shaft position callback used by RPM calculation logic.
 *
 * This callback should always be the first of trigger callbacks because other callbacks depend of values
 * updated here.
 * This callback is invoked on interrupt thread.
 */
void rpmShaftPositionCallback(uint32_t trgEventIndex, efitick_t nowNt) {

	bool alwaysInstantRpm = engineConfiguration->alwaysInstantRpm;

	RpmCalculator *rpmState = &engine->rpmCalculator;

	if (trgEventIndex == 0) {
		if (HAVE_CAM_INPUT()) {
			engine->triggerCentral.validateCamVvtCounters();
		}

		bool hadRpmRecently = rpmState->checkIfSpinning(nowNt);

		float periodSeconds = engine->rpmCalculator.lastTdcTimer.getElapsedSecondsAndReset(nowNt);

		if (hadRpmRecently) {
		/**
		 * Four stroke cycle is two crankshaft revolutions
		 *
		 * We always do '* 2' because the event signal is already adjusted to 'per engine cycle'
		 * and each revolution of crankshaft consists of two engine cycles revolutions
		 *
		 */
			if (!alwaysInstantRpm) {
				if (periodSeconds == 0) {
					rpmState->setRpmValue(NOISY_RPM);
					rpmState->rpmRate = 0;
				} else {
					int mult = (int)getEngineCycle(getEngineRotationState()->getOperationMode()) / 360;
					float rpm = 60 * mult / periodSeconds;

					auto rpmDelta = rpm - rpmState->previousRpmValue;
					rpmState->rpmRate = rpmDelta / (mult * periodSeconds);

					rpmState->setRpmValue(rpm > UNREALISTIC_RPM ? NOISY_RPM : rpm);
				}
			}
		} else {
			// we are here only once trigger is synchronized for the first time
			// while transitioning  from 'spinning' to 'running'
			engine->triggerCentral.instantRpm.movePreSynchTimestamps();
		}

		rpmState->onNewEngineCycle();
	}

	// Always update instant RPM even when not spinning up
	engine->triggerCentral.instantRpm.updateInstantRpm(
		engine->triggerCentral.triggerShape, &engine->triggerCentral.triggerFormDetails,
		trgEventIndex, nowNt);

	float instantRpm = engine->triggerCentral.instantRpm.getInstantRpm();
	if (alwaysInstantRpm) {
		rpmState->setRpmValue(instantRpm);
	} else if (rpmState->isSpinningUp()) {
		rpmState->assignRpmValue(instantRpm);
	}
}

float RpmCalculator::getSecondsSinceEngineStart(efitick_t nowNt) const {
	return engineStartTimer.getElapsedSeconds(nowNt);
}


/**
 * This callback has nothing to do with actual engine control, it just sends a Top Dead Center mark to the rusEfi console
 * digital sniffer.
 */
static void onTdcCallback(void *) {
#if EFI_UNIT_TEST
	if (!engine->needTdcCallback) {
		return;
	}
#endif /* EFI_UNIT_TEST */

	float rpm = Sensor::getOrZero(SensorType::Rpm);
	addEngineSnifferTdcEvent(rpm);
#if EFI_TOOTH_LOGGER
	LogTriggerTopDeadCenter(getTimeNowNt());
#endif /* EFI_TOOTH_LOGGER */
}

/**
 * This trigger callback schedules the actual physical TDC callback in relation to trigger synchronization point.
 */
void tdcMarkCallback(
		uint32_t trgEventIndex, efitick_t edgeTimestamp) {
	bool isTriggerSynchronizationPoint = trgEventIndex == 0;
	if (isTriggerSynchronizationPoint && getTriggerCentral()->isEngineSnifferEnabled) {

#if EFI_UNIT_TEST
		if (!engine->tdcMarkEnabled) {
			return;
		}
#endif // EFI_UNIT_TEST


		// two instances of scheduling_s are needed to properly handle event overlap
		int revIndex2 = getRevolutionCounter() % 2;
		float rpm = Sensor::getOrZero(SensorType::Rpm);
		// todo: use tooth event-based scheduling, not just time-based scheduling
		if (isValidRpm(rpm)) {
			angle_t tdcPosition = tdcPosition();
			// we need a positive angle offset here
			wrapAngle(tdcPosition, "tdcPosition", ObdCode::CUSTOM_ERR_6553);
			scheduleByAngle(&engine->tdcScheduler[revIndex2], edgeTimestamp, tdcPosition, onTdcCallback);
		}
	}
}

/**
 * Schedules a callback 'angle' degree of crankshaft from now.
 * The callback would be executed once after the duration of time which
 * it takes the crankshaft to rotate to the specified angle.
 */
efitick_t scheduleByAngle(scheduling_s *timer, efitick_t edgeTimestamp, angle_t angle,
		action_s action) {
	float delayUs = engine->rpmCalculator.oneDegreeUs * angle;

	// 'delayNt' is below 10 seconds here so we use 32 bit type for performance reasons
	int32_t delayNt = USF2NT(delayUs);
	efitick_t delayedTime = edgeTimestamp + efidur_t{delayNt};

	engine->scheduler.schedule("angle", timer, delayedTime, action);

	return delayedTime;
}

#else
RpmCalculator::RpmCalculator() :
		StoredValueSensor(SensorType::Rpm, 0)
{

}

#endif /* EFI_SHAFT_POSITION_INPUT */

