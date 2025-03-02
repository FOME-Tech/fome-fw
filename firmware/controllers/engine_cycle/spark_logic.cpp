/*
 * @file spark_logic.cpp
 *
 * @date Sep 15, 2016
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"

#include "spark_logic.h"

#include "utlist.h"
#include "event_queue.h"

#include "knock_logic.h"

#if EFI_ENGINE_CONTROL

/**
 * @param cylinderIndex from 0 to cylinderCount, not cylinder number
 */
static int getIgnitionPinForIndex(int cylinderIndex, ignition_mode_e ignitionMode) {
	switch (ignitionMode) {
	case IM_ONE_COIL:
		return 0;
	case IM_WASTED_SPARK: {
		if (engineConfiguration->cylindersCount == 1) {
			// we do not want to divide by zero
			return 0;
		}
		return cylinderIndex % (engineConfiguration->cylindersCount / 2);
	}
	case IM_INDIVIDUAL_COILS:
		return cylinderIndex;
	case IM_TWO_COILS:
		return cylinderIndex % 2;

	default:
		firmwareError(ObdCode::CUSTOM_OBD_IGNITION_MODE, "Invalid ignition mode getIgnitionPinForIndex(): %d", engineConfiguration->ignitionMode);
		return 0;
	}
}

angle_t OneCylinder::getSparkAngle(angle_t lateAdjustment) const {
	// Compute the final ignition timing including all "late" adjustments
	angle_t finalIgnitionTiming = m_timingAdvance + lateAdjustment;

	// 10 ATDC ends up as 710, convert it to -10 so we can log and clamp correctly
	if (finalIgnitionTiming > 360) {
		finalIgnitionTiming -= 720;
	}

	// Clamp the final ignition timing to the configured limits
	// finalIgnitionTiming is deg BTDC
	// minimumIgnitionTiming limits maximium retard
	// maximumIgnitionTiming limits maximum advance
	finalIgnitionTiming = clampF(engineConfiguration->minimumIgnitionTiming, finalIgnitionTiming, engineConfiguration->maximumIgnitionTiming);

	engine->outputChannels.ignitionAdvanceCyl[m_cylinderNumber] = finalIgnitionTiming;

	return
		// Negate because timing *before* TDC, and we schedule *after* TDC
		- finalIgnitionTiming
		// Offset by this cylinder's position in the cycle
		+ getAngleOffset();
}

uint16_t IgnitionEvent::calculateIgnitionOutputMask() const {
	const int index = getIgnitionPinForIndex(cylinderIndex, m_ignitionMode);
	const int coilIndex = getCylinderNumberAtIndex(index);

	uint16_t outputsMask = 1 << coilIndex;

	// If wasted spark, find the paired coil in addition to "main" output for this cylinder
	if (m_ignitionMode == IM_WASTED_SPARK) {
		int secondIndex = index + engineConfiguration->cylindersCount / 2;
		int secondCoilIndex = getCylinderNumberAtIndex(secondIndex);
		outputsMask |= 1 << secondCoilIndex;
	}

	return outputsMask;
}

angle_t IgnitionEvent::calculateSparkAngle() const {
	angle_t sparkAngle = engine->cylinders[cylinderNumber].getSparkAngle(
		// Pull any extra timing for knock retard
		- engine->module<KnockController>()->getKnockRetard()
	);

	efiAssert(ObdCode::CUSTOM_SPARK_ANGLE_1, !std::isnan(sparkAngle), "sparkAngle#1", 0);
	wrapAngle(sparkAngle, "findAngle#2", ObdCode::CUSTOM_ERR_6550);

	return sparkAngle;
}

static void prepareCylinderIgnitionSchedule(angle_t dwellAngleDuration, floatms_t sparkDwell, IgnitionEvent *event) {
	// todo: clean up this implementation? does not look too nice as is.

	const int realCylinderNumber = getCylinderNumberAtIndex(event->cylinderIndex);

	// let's save planned duration so that we can later compare it with reality
	event->sparkDwell = sparkDwell;

	// Stash which cylinder we're scheduling so that knock sensing knows which
	// cylinder just fired
	event->cylinderNumber = realCylinderNumber;

	auto sparkAngle = event->calculateSparkAngle();

	auto ignitionMode = getCurrentIgnitionMode();

	// On an odd cylinder (or odd fire) wasted spark engine, map outputs as if in sequential.
	// During actual scheduling, the events just get scheduled every 360 deg instead
	// of every 720 deg.
	if (ignitionMode == IM_WASTED_SPARK && engine->engineState.useOddFireWastedSpark) {
		ignitionMode = IM_INDIVIDUAL_COILS;
	}

	angle_t dwellStartAngle = sparkAngle - dwellAngleDuration;
	efiAssertVoid(ObdCode::CUSTOM_ERR_6590, !std::isnan(dwellStartAngle), "findAngle#5");

	assertAngleRange(dwellStartAngle, "findAngle dwellStartAngle", ObdCode::CUSTOM_ERR_6550);
	wrapAngle(dwellStartAngle, "findAngle#7", ObdCode::CUSTOM_ERR_6550);

	event->m_ignitionMode = ignitionMode;
	event->dwellAngle = dwellStartAngle;

	engine->outputChannels.currentIgnitionMode = static_cast<uint8_t>(ignitionMode);
}

static void chargeTrailingSpark(IgnitionOutputPin* pin) {
	pin->setHigh();
}

static void fireTrailingSpark(IgnitionOutputPin* pin) {
	pin->setLow();
}

void fireSparkAndPrepareNextSchedule(IgnitionContext ctx) {
	efitick_t nowNt = getTimeNowNt();
	IgnitionEvent *event = &engine->ignitionEvents.elements[ctx.eventIndex];

	float actualDwellMs = event->actualDwellTimer.getElapsedSeconds(nowNt) * 1e3;
	float minDwell = 0.8f * event->sparkDwell;
	if (!ctx.isOverdwellProtect && actualDwellMs < minDwell) {
		float extraTimeUs = (minDwell - actualDwellMs) * 1e3;

		if (extraTimeUs < 10) {
			extraTimeUs = 10;
		}

		efitick_t delayedFireTime = nowNt + efidur_t{(uint32_t)USF2NT(extraTimeUs)};

		// cancel multispark in case of underdwell
		ctx.sparksRemaining = 0;

		// re-schedule ourselves at a later time once enough dwell has elapsed
		// This is fine to do because it will retard the effective ignition timing, but 
		// ensure the coil has enough energy to actually fire (we would rather retard timing than misfire)
		engine->scheduler.schedule("firing", &event->sparkEvent.scheduling, delayedFireTime, { fireSparkAndPrepareNextSchedule, ctx });
		return;
	}

#if EFI_UNIT_TEST
	if (engine->onIgnitionEvent) {
		engine->onIgnitionEvent(ctx, false);
	}
#endif

	uint16_t mask = ctx.outputsMask;
	size_t idx = 0;

	while(mask) {
		if (mask & 0x1) {
			enginePins.coils[idx].setLow();
		}

		mask = mask >> 1;
		idx++;
	}

#if EFI_TOOTH_LOGGER
	LogTriggerCoilState(nowNt, false);
#endif // EFI_TOOTH_LOGGER

#if EFI_TUNER_STUDIO
	// ratio of desired dwell duration to actual dwell duration gives us some idea of how good is input trigger jitter
	engine->outputChannels.dwellAccuracyRatio = actualDwellMs / event->sparkDwell;
#endif

	// now that we've just fired a coil let's prepare the new schedule for the next engine revolution

	angle_t dwellAngleDuration = engine->ignitionState.dwellAngle;
	floatms_t sparkDwell = engine->ignitionState.getDwell();
	if (std::isnan(dwellAngleDuration) || std::isnan(sparkDwell)) {
		// we are here if engine has just stopped
		return;
	}

	// If there are more sparks to fire, schedule them
	if (ctx.sparksRemaining > 0 && !ctx.isOverdwellProtect) {
		ctx.sparksRemaining--;

		efitick_t nextDwellStart = nowNt + engine->engineState.multispark.delay;
		efitick_t nextFiring = nextDwellStart + engine->engineState.multispark.dwell;

		// We can schedule both of these right away, since we're going for "asap" not "particular angle"
		engine->scheduler.schedule("dwell", &event->dwellStartTimer, nextDwellStart, { &turnSparkPinHigh, ctx });
		engine->scheduler.schedule("firing", &event->sparkEvent.scheduling, nextFiring, { fireSparkAndPrepareNextSchedule, ctx });
	} else {
		if (engineConfiguration->enableTrailingSparks && !ctx.isOverdwellProtect) {
			// Trailing sparks are enabled - schedule an event for the corresponding trailing coil
			scheduleByAngle(
				&event->trailingSparkFire, nowNt, engine->engineState.trailingSparkAngle,
				{ &fireTrailingSpark, &enginePins.trailingCoils[event->cylinderNumber] }
			);
		}

		// If all events have been scheduled, prepare for next time.
		prepareCylinderIgnitionSchedule(dwellAngleDuration, sparkDwell, event);
	}

	engine->onSparkFireKnockSense(event->cylinderNumber, nowNt);
}

void turnSparkPinHigh(IgnitionContext ctx) {
	efitick_t nowNt = getTimeNowNt();

	uint16_t mask = ctx.outputsMask;
	size_t idx = 0;

	while(mask) {
		if (mask & 0x1) {
			enginePins.coils[idx].setHigh();
		}

		mask = mask >> 1;
		idx++;
	}

	IgnitionEvent *event = &engine->ignitionEvents.elements[ctx.eventIndex];

	event->actualDwellTimer.reset(nowNt);

#if EFI_UNIT_TEST
	if (engine->onIgnitionEvent) {
		engine->onIgnitionEvent(ctx, true);
	}
#endif

#if EFI_TOOTH_LOGGER
	LogTriggerCoilState(nowNt, true);
#endif // EFI_TOOTH_LOGGER

	if (engineConfiguration->enableTrailingSparks) {
		IgnitionOutputPin *output = &enginePins.trailingCoils[event->cylinderNumber];
		// Trailing sparks are enabled - schedule an event for the corresponding trailing coil
		scheduleByAngle(
			&event->trailingSparkCharge, nowNt, engine->engineState.trailingSparkAngle,
			{ &chargeTrailingSpark, output }
		);
	}
}

static void scheduleSparkEvent(bool limitedSpark, IgnitionEvent *event, float dwellMs, float dwellAngle, float sparkAngle, efitick_t edgeTimestamp, float currentPhase, float nextPhase) {
	float angleOffset = dwellAngle - currentPhase;
	if (angleOffset < 0) {
		angleOffset += engine->engineState.engineCycle;
	}

	engine->engineState.sparkCounter++;
	event->wasSparkLimited = limitedSpark;

	IgnitionContext ctx;
	ctx.outputsMask = event->calculateIgnitionOutputMask();
	ctx.eventIndex = event->cylinderIndex;
	ctx.sparksRemaining = limitedSpark ? 0 : engine->engineState.multispark.count;

	efitick_t chargeTime;

	/**
	 * The start of charge is always within the current trigger event range, so just plain time-based scheduling
	 */
	if (!limitedSpark) {
		/**
		 * Note how we do not check if spark is limited or not while scheduling 'spark down'
		 * This way we make sure that coil dwell started while spark was enabled would fire and not burn
		 * the coil.
		 */
		chargeTime = scheduleByAngle(&event->dwellStartTimer, edgeTimestamp, angleOffset, { &turnSparkPinHigh, ctx });
	}

	/**
	 * Spark event is often happening during a later trigger event timeframe
	 */

	efiAssertVoid(ObdCode::CUSTOM_ERR_6591, !std::isnan(sparkAngle), "findAngle#4");
	assertAngleRange(sparkAngle, "findAngle#a5", ObdCode::CUSTOM_ERR_6549);

	bool scheduled = engine->module<TriggerScheduler>()->scheduleOrQueue(
		&event->sparkEvent, edgeTimestamp, sparkAngle,
		{ fireSparkAndPrepareNextSchedule, ctx },
		currentPhase, nextPhase);

	if (!scheduled && !limitedSpark) {
		// If spark firing wasn't already scheduled, schedule the overdwell event at
		// 1.5x nominal dwell, should the trigger disappear before its scheduled for real
		efitick_t fireTime = chargeTime + (uint32_t)MSF2NT(1.5f * dwellMs);
		ctx.isOverdwellProtect = true;
		engine->scheduler.schedule("overdwell", &event->sparkEvent.scheduling, fireTime, { fireSparkAndPrepareNextSchedule, ctx });
	}
}

void initializeIgnitionActions() {
	IgnitionEventList *list = &engine->ignitionEvents;
	angle_t dwellAngle = engine->ignitionState.dwellAngle;
	floatms_t sparkDwell = engine->ignitionState.getDwell();
	if (std::isnan(engine->cylinders[0].getIgnitionTimingBtdc()) || std::isnan(dwellAngle)) {
		// error should already be reported
		// need to invalidate previous ignition schedule
		list->isReady = false;
		return;
	}
	efiAssertVoid(ObdCode::CUSTOM_ERR_6592, engineConfiguration->cylindersCount > 0, "cylindersCount");

	for (size_t cylinderIndex = 0; cylinderIndex < engineConfiguration->cylindersCount; cylinderIndex++) {
		list->elements[cylinderIndex].cylinderIndex = cylinderIndex;
		prepareCylinderIgnitionSchedule(dwellAngle, sparkDwell, &list->elements[cylinderIndex]);
	}
	list->isReady = true;
}

static void prepareIgnitionSchedule() {
	ScopePerf perf(PE::PrepareIgnitionSchedule);
	
	/**
	 * TODO: warning. there is a bit of a hack here, todo: improve.
	 * currently output signals/times dwellStartTimer from the previous revolutions could be
	 * still used because they have crossed the revolution boundary
	 * but we are already re-purposing the output signals, but everything works because we
	 * are not affecting that space in memory. todo: use two instances of 'ignitionSignals'
	 */
	operation_mode_e operationMode = getEngineRotationState()->getOperationMode();
	float maxAllowedDwellAngle = (int) (getEngineCycle(operationMode) / 2); // the cast is about making Coverity happy

	if (getCurrentIgnitionMode() == IM_ONE_COIL) {
		maxAllowedDwellAngle = getEngineCycle(operationMode) / engineConfiguration->cylindersCount / 1.1;
	}

	if (engine->ignitionState.dwellAngle == 0) {
		warning(ObdCode::CUSTOM_ZERO_DWELL, "dwell is zero?");
	}
	if (engine->ignitionState.dwellAngle > maxAllowedDwellAngle) {
		warning(ObdCode::CUSTOM_DWELL_TOO_LONG, "dwell angle too long: %.2f", engine->ignitionState.dwellAngle);
	}

	// todo: add some check for dwell overflow? like 4 times 6 ms while engine cycle is less then that

	initializeIgnitionActions();
}

void onTriggerEventSparkLogic(efitick_t edgeTimestamp, float currentPhase, float nextPhase) {
	ScopePerf perf(PE::OnTriggerEventSparkLogic);

	if (!engineConfiguration->isIgnitionEnabled) {
		return;
	}

	bool limitedSpark = !getLimpManager()->allowIgnition().value;

	const floatms_t dwellMs = engine->ignitionState.getDwell();
	if (std::isnan(dwellMs) || dwellMs <= 0) {
		warning(ObdCode::CUSTOM_DWELL, "invalid dwell to handle: %.2f", dwellMs);
		return;
	}

	if (!engine->ignitionEvents.isReady) {
		prepareIgnitionSchedule();
	}

	/**
	 * Ignition schedule is defined once per revolution
	 * See initializeIgnitionActions()
	 */

	// Only apply odd cylinder count wasted logic if:
	// - odd cyl count
	// - current mode is wasted spark
	// - four stroke
	bool enableOddCylinderWastedSpark =
		engine->engineState.useOddFireWastedSpark
		&& getCurrentIgnitionMode() == IM_WASTED_SPARK;

	if (engine->ignitionEvents.isReady) {
		for (size_t i = 0; i < engineConfiguration->cylindersCount; i++) {
			IgnitionEvent *event = &engine->ignitionEvents.elements[i];

			angle_t dwellAngle = event->dwellAngle;

			angle_t sparkAngleAdjust = 0;

			bool isOddCylWastedEvent = false;
			if (enableOddCylinderWastedSpark) {
				auto dwellAngleWastedEvent = dwellAngle + 360;
				if (dwellAngleWastedEvent > 720) {
					dwellAngleWastedEvent -= 720;
				}

				// Check whether this event hits 360 degrees out from now (ie, wasted spark),
				// and if so, twiddle the dwell and spark angles so it happens now instead
				isOddCylWastedEvent = isPhaseInRange(dwellAngleWastedEvent, currentPhase, nextPhase);

				if (isOddCylWastedEvent) {
					dwellAngle = dwellAngleWastedEvent;

					sparkAngleAdjust = 360;
				}
			}

			if (!isOddCylWastedEvent && !isPhaseInRange(dwellAngle, currentPhase, nextPhase)) {
				continue;
			}

			angle_t sparkAngle = sparkAngleAdjust + event->calculateSparkAngle();
			if (sparkAngle > 720) {
				sparkAngle -= 720;
			}
			if (std::isnan(sparkAngle)) {
				warning(ObdCode::CUSTOM_ADVANCE_SPARK, "NaN advance");
				continue;
			}

			if (i == 0 && engineConfiguration->artificialTestMisfire && (getRevolutionCounter() % ((int)engineConfiguration->scriptSetting[5]) == 0)) {
				// artificial misfire on cylinder #1 for testing purposes
				// enable artificialMisfire
				// set_fsio_setting 6 20
				warning(ObdCode::CUSTOM_ARTIFICIAL_MISFIRE, "artificial misfire on cylinder #1 for testing purposes %lu", engine->engineState.sparkCounter);
				continue;
			}
#if EFI_LAUNCH_CONTROL
			if (engine->softSparkLimiter.shouldSkip()) {
				continue;
			}
#endif // EFI_LAUNCH_CONTROL

#if EFI_ANTILAG_SYSTEM && EFI_LAUNCH_CONTROL
			if (engine->ALSsoftSparkLimiter.shouldSkip()) {
				continue;
			}
			auto ALSSkipRatio = engineConfiguration->ALSSkipRatio;
			engine->ALSsoftSparkLimiter.setTargetSkipRatio(ALSSkipRatio);
#endif // EFI_ANTILAG_SYSTEM

			scheduleSparkEvent(limitedSpark, event, dwellMs, dwellAngle, sparkAngle, edgeTimestamp, currentPhase, nextPhase);
		}
	}
}

/**
 * Number of sparks per physical coil
 * @see getNumberOfInjections
 */
int getNumberOfSparks(ignition_mode_e mode) {
	switch (mode) {
	case IM_ONE_COIL:
		return engineConfiguration->cylindersCount;
	case IM_TWO_COILS:
		return engineConfiguration->cylindersCount / 2;
	case IM_INDIVIDUAL_COILS:
		return 1;
	case IM_WASTED_SPARK:
		return 2;
	default:
		firmwareError(ObdCode::CUSTOM_ERR_IGNITION_MODE, "Unexpected ignition_mode_e %d", mode);
		return 1;
	}
}

/**
 * @see getInjectorDutyCycle
 */
percent_t getCoilDutyCycle(float rpm) {
	floatms_t totalPerCycle = engine->ignitionState.getDwell() * getNumberOfSparks(getCurrentIgnitionMode());
	floatms_t engineCycleDuration = getCrankshaftRevolutionTimeMs(rpm) * (getEngineRotationState()->getOperationMode() == TWO_STROKE ? 1 : 2);
	return 100 * totalPerCycle / engineCycleDuration;
}

#endif // EFI_ENGINE_CONTROL
