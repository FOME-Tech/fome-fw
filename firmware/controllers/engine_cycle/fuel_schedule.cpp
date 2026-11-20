/**
 * @file fuel_schedule.cpp
 *
 * Handles injection scheduling
 */

#include "pch.h"
#include "fuel_math.h"

#if EFI_ENGINE_CONTROL

extern bool printFuelDebug;

// Durations under 50us-ish aren't safe for the scheduler as their order may be swapped, resulting in a stuck open
// injector, see https://github.com/rusefi/rusefi/pull/596 for more details
static constexpr floatms_t minInjectionDurationMs = 0.050f;

void startInjection(InjectorContext ctx) {
	forEachSetBit(ctx.outputsMask, [&ctx](size_t idx) {
		enginePins.injectors[idx].open();

		if (ctx.stage2Active) {
			enginePins.injectorsStage2[idx].open();
		}
	});
}

void endInjection(InjectorContext ctx) {
	forEachSetBit(ctx.outputsMask, [](size_t idx) { enginePins.injectors[idx].close(); });

	if (ctx.splitDurationUs > 0) {
		efitick_t openTime = getTimeNowNt() + MS2NT(2);
		efitick_t closeTime = openTime + US2NT(ctx.splitDurationUs);

		// Zero out the split duration so it doesn't repeat
		ctx.splitDurationUs = 0;

		TimedAction secondHalf[] = {
				{openTime, {&startInjection, ctx}},
				{closeTime, {&endInjection, ctx}},
		};

		if (getScheduler()->scheduleTogether("split inj", secondHalf, efi::size(secondHalf))) {
			// The second half's end prepares for the next cycle
			return;
		}

		// Couldn't schedule the second half: carry on as if it was done
	}

	// No splits remaining, prepare for next cycle
	if (ctx.eventIndex < efi::size(getFuelSchedule()->elements)) {
		getFuelSchedule()->elements[ctx.eventIndex].update();
	}
}

void endInjectionStage2(InjectorContext ctx) {
	forEachSetBit(ctx.outputsMask, [](size_t idx) { enginePins.injectorsStage2[idx].close(); });
}

// Whether a non-sequential pulse from this event should also fire the injector belonging to the event
// at otherIndex (in firing order).
//
// Each event switches mode at the end of its own pulse, so a switch to sequential rolls through the
// cylinders one at a time. Once a cylinder's own event has gone sequential, its next intake should
// get only its own full pulse. A batch/simultaneous pulse from another event that hasn't switched yet
// would add to that, so it must leave this injector out. Up to that point everything fires as
// normal, so the last non-sequential intake is still complete. The result is every cylinder gets
// exactly one cycle of fuel per intake through the transition.
bool InjectionEvent::shouldFireInjectorAtIndex(size_t otherIndex) const {
	return engine->injectionEvents.elements[otherIndex].m_injectionMode != IM_SEQUENTIAL;
}

uint16_t InjectionEvent::calculateInjectorOutputMask() const {
	uint16_t mask = 0;

	switch (m_injectionMode) {
		case IM_SIMULTANEOUS:
			// Simultaneous mode fires all injectors
			for (size_t i = 0; i < engine->engineState.cylinderCount; i++) {
				if (i == ownIndex || shouldFireInjectorAtIndex(i)) {
					mask |= 1 << getCylinderNumberAtIndex(i);
				}
			}
			break;
		case IM_SINGLE_POINT:
			// Single point only fires injector 1
			mask = 1;
			break;
		case IM_BATCH: {
			// In batch mode, also fire the cylinder 360 degrees out to support "two-wire batch" mode

			// Compute the position of this cylinder's twin in the firing order
			// Each injector gets fired as a primary (the same as sequential), but also
			// fires the injector 360 degrees later in the firing order.
			size_t twinIndex = (ownIndex + (engine->engineState.cylinderCount / 2)) % engine->engineState.cylinderCount;

			if (shouldFireInjectorAtIndex(twinIndex)) {
				mask |= 1 << getCylinderNumberAtIndex(twinIndex);
			}
		}
			// falls through
		case IM_SEQUENTIAL:
			// In batch+sequential, fire this cylinder's injector
			mask |= 1 << cylinderNumber;
			break;
	}

	return mask;
}

float getInjectionLeadFraction() {
	switch (engineConfiguration->injectionTimingMode) {
		case InjectionTimingMode::Start:
			return 0;
		case InjectionTimingMode::Center:
			return 0.5f;
		case InjectionTimingMode::End:
		default:
			return 1;
	}
}

expected<CycleEventInstance>
InjectionEvent::pollDue(const EnginePhaseInfo& phase, float leadFraction, floatus_t oneDegreeUs) {
	angle_t engineCycle = getEngineState()->engineCycle;
	angle_t tableAngle = getEngineState()->injectionOffset;

	// Where the next pulse goes around its anchor, from the fast callback's estimate of how long it will be
	angle_t estimatedDurationAngle = MS2US(m_durationEstimateMs) / oneDegreeUs;
	if (!(estimatedDurationAngle >= 0)) {
		// NaN or negative: there's nothing to place. The instance still comes due at its anchor, where the
		// actual duration is checked (and warned about).
		estimatedDurationAngle = 0;
	}

	OutputShape shape{
			.anchorPhase = wrapToPeriod(tableAngle + engine->cylinders[cylinderNumber].getAngleOffset(), engineCycle),
			.leadDeg = leadFraction * estimatedDurationAngle,
			.tailDeg = (1 - leadFraction) * estimatedDurationAngle,
	};

	return m_tracker.poll(shape, phase, engineCycle);
}

bool InjectionEvent::skipIfNotToInject(const CycleEventInstance& instance, const EnginePhaseInfo& phase) {
	auto& state = *getEngineState();
	bool skip = false;

	if (instance.afterLabelJump && instance.startFromNow < 0) {
		// Only late because the engine phase was just relabeled, so this cycle may already have had its fuel
		// under the old phase
		state.injectionSkippedPhaseJumpCount++;
		skip = true;
	} else if (!instance.servable) {
		// The end of injection it was aiming for has gone by
		state.injectionSkippedLateCount++;
		skip = true;
	} else if (!getLimpManager()->allowInjection().value) {
		skip = true;
	}

	if (skip) {
		skipInstance(instance, phase);
	}

	return skip;
}

void InjectionEvent::skipInstance(const CycleEventInstance& instance, const EnginePhaseInfo& phase) {
	// No pulse, so no pulse end to choose the injection mode for the next one: do it now
	update();
	m_tracker.resolve(instance, phase);
}

bool InjectionEvent::injectInstance(
		const CycleEventInstance& instance, const EnginePhaseInfo& phase, float leadFraction, floatus_t oneDegreeUs) {
	// Resolved either way. If it couldn't be scheduled, it's a skip, but choosing the injection mode for the
	// next pulse is left to the caller (see FuelSchedule's handleTooth).
	m_tracker.resolve(instance, phase);
	return scheduleInstance(instance, phase, leadFraction, oneDegreeUs);
}

bool InjectionEvent::scheduleInstance(
		const CycleEventInstance& instance, const EnginePhaseInfo& phase, float leadFraction, floatus_t oneDegreeUs) {
	auto& state = *getEngineState();

	bool doSplitInjection = engine->engineState.doSplitInjection;

	// Select fuel mass from the correct cylinder
	auto cycleMassGrams = engine->cylinders[this->cylinderNumber].getInjectionMass();

	float injectionMassGrams = cycleMassGrams * getInjectionModeDurationMultiplier(m_injectionMode);

	// Perform wall wetting adjustment on fuel mass, not duration, so that
	// it's correct during fuel pressure (injector flow) or battery voltage (deadtime) transients
	// TODO: is it correct to wall wet on both pulses?
	injectionMassGrams = wallFuel.adjust(injectionMassGrams);

	// Disable staging in split injection mode
	float stage2Fraction = doSplitInjection ? 0 : getEngineState()->injectionStage2Fraction;

	// Compute fraction of fuel on stage 2, remainder goes on stage 1
	const float injectionMassStage2 = stage2Fraction * injectionMassGrams;
	float injectionMassStage1 = injectionMassGrams - injectionMassStage2;

	{
		// Log this fuel as consumed

#ifdef MODULE_TRIP_ODO
		int numberOfInjections = getNumberOfInjections(m_injectionMode);

		float actualInjectedMass = numberOfInjections * (injectionMassStage1 + injectionMassStage2);

		engine->module<TripOdometer>()->consumeFuel(actualInjectedMass, phase.timestamp);
#endif // MODULE_TRIP_ODO
	}

	// The pulse is placed by the whole of stage 1. With split injection that's both halves together.
	float leadMassGrams = injectionMassStage1;

	if (doSplitInjection) {
		// If in split mode, do the injection in two halves
		injectionMassStage1 = injectionMassStage1 / 2;
	}

	const floatms_t injectionDurationStage1 =
			engine->module<InjectorModelPrimary>()->getInjectionDuration(injectionMassStage1);
	const floatms_t injectionDurationStage2 =
			injectionMassStage2 > 0
					? engine->module<InjectorModelSecondary>()->getInjectionDuration(injectionMassStage2)
					: 0;

#if EFI_PRINTF_FUEL_DETAILS
	if (printFuelDebug) {
		printf("fuel injectionDuration=%.2fms adjusted=%.2fms\n",
			   getEngineState()->injectionDuration,
			   injectionDurationStage1);
	}
#endif /*EFI_PRINTF_FUEL_DETAILS */

	if (this->cylinderNumber == 0) {
		engine->outputChannels.actualLastInjection = injectionDurationStage1;
		engine->outputChannels.actualLastInjectionStage2 = injectionDurationStage2;
	}

	if (std::isnan(injectionDurationStage1) || std::isnan(injectionDurationStage2)) {
		warning(ObdCode::CUSTOM_OBD_NAN_INJECTION, "NaN injection pulse");
		return false;
	}
	if (injectionDurationStage1 < 0) {
		warning(ObdCode::CUSTOM_OBD_NEG_INJECTION, "Negative injection pulse %.2f", injectionDurationStage1);
		return false;
	}

	// If somebody commanded an impossibly short injection, do nothing.
	if (injectionDurationStage1 < minInjectionDurationMs) {
		return false;
	}

	floatus_t durationUsStage1 = MS2US(injectionDurationStage1);
	floatus_t durationUsStage2 = MS2US(injectionDurationStage2);

	// Only bother with the second stage if it's long enough to be relevant
	bool hasStage2Injection = durationUsStage2 > 50;

	InjectorContext ctx;
	ctx.eventIndex = ownIndex;
	ctx.stage2Active = hasStage2Injection;
	ctx.outputsMask = calculateInjectorOutputMask();

	if (doSplitInjection) {
		ctx.splitDurationUs = durationUsStage1;
	}

#if EFI_PRINTF_FUEL_DETAILS
	if (printFuelDebug) {
		printf("handleFuelInjectionEvent fuelout %06x injection_duration %dus engineCycleDuration=%.1fms\t\n",
			   ctx.outputsMask,
			   (int)durationUsStage1,
			   (int)MS2US(getCrankshaftRevolutionTimeMs(Sensor::getOrZero(SensorType::Rpm))) / 1000.0);
	}
#endif /*EFI_PRINTF_FUEL_DETAILS */

	// Start so that the pulse sits around its anchor as the table says, by the duration actually being
	// injected. If that's already gone by, start now.
	floatms_t leadDurationMs = doSplitInjection
									 ? engine->module<InjectorModelPrimary>()->getInjectionDuration(leadMassGrams)
									 : injectionDurationStage1;
	float startFromNow = instance.anchorFromNow - leadFraction * MS2US(leadDurationMs) / oneDegreeUs;
	bool isLate = startFromNow < 0;
	if (isLate) {
		startFromNow = 0;
	}

	// Opening (stage 1 + stage 2 open together), closing stage 1, and closing stage 2 if applicable.
	// These are scheduled all together or not at all: an opening without its closing would leave the
	// injector open.
	efitick_t startTime = getTimeAfterAngle(phase.timestamp, startFromNow);

	TimedAction actions[3] = {
			{startTime, {&startInjection, ctx}},
			{startTime + US2NT((int)durationUsStage1), {&endInjection, ctx}},
	};
	size_t actionCount = 2;

	if (hasStage2Injection) {
		actions[actionCount++] = {startTime + US2NT((int)durationUsStage2), {&endInjectionStage2, ctx}};
	}

	if (!getScheduler()->scheduleTogether("inj", actions, actionCount)) {
		state.injectionSkippedPoolCount++;
		return false;
	}

	if (isLate) {
		state.injectionLateCount++;
	}

#if EFI_UNIT_TEST
	printf("scheduling injection angle=%.2f/delay=%d injectionDuration=%d %d\r\n",
		   startFromNow,
		   (int)NT2US(startTime - phase.timestamp),
		   (int)durationUsStage1,
		   (int)durationUsStage2);
#endif

	return true;
}

FuelSchedule::FuelSchedule() {
	for (int cylinderIndex = 0; cylinderIndex < MAX_CYLINDER_COUNT; cylinderIndex++) {
		elements[cylinderIndex].setIndex(cylinderIndex);
	}
}

WallFuel& InjectionEvent::getWallFuel() {
	return wallFuel;
}

void FuelSchedule::invalidate() {
	isReady = false;
}

void FuelSchedule::resetTrackers() {
	for (auto& element : elements) {
		element.resetTracker();
	}
}

/**
 * @returns false if injections can't be placed yet (no engine speed, or no injection phase), true if success
 */
bool InjectionEvent::update() {
	cylinderNumber = getCylinderNumberAtIndex(ownIndex);

	if (std::isnan(getEngineRotationState()->getOneDegreeUs()) || std::isnan(getEngineState()->injectionOffset)) {
		return false;
	}

	injection_mode_e mode = getCurrentInjectionMode();
	m_injectionMode = mode;

	engine->outputChannels.currentInjectionMode = static_cast<uint8_t>(mode);

	return true;
}

void FuelSchedule::addFuelEvents() {
	for (size_t cylinderIndex = 0; cylinderIndex < engine->engineState.cylinderCount; cylinderIndex++) {
		bool result = elements[cylinderIndex].update();

		if (!result) {
			invalidate();
			return;
		}
	}

	// We made it through all cylinders, mark the schedule as ready so it can be used
	isReady = true;
}

void InjectionEvent::updateDurationEstimate() {
	// The same fuel calculation as onTriggerTooth, but without committing anything (wall wetting is only
	// previewed). With split injection this is both halves together.
	bool doSplitInjection = engine->engineState.doSplitInjection;

	float massGrams =
			engine->cylinders[cylinderNumber].getInjectionMass() * getInjectionModeDurationMultiplier(m_injectionMode);
	massGrams = wallFuel.preview(massGrams);

	float stage2Fraction = doSplitInjection ? 0 : getEngineState()->injectionStage2Fraction;
	float stage1MassGrams = massGrams * (1 - stage2Fraction);

	m_durationEstimateMs = engine->module<InjectorModelPrimary>()->getInjectionDuration(stage1MassGrams);
}

void FuelSchedule::updateDurationEstimates() {
	for (size_t i = 0; i < engine->engineState.cylinderCount; i++) {
		elements[i].updateDurationEstimate();
	}
}

// Everything a tooth does for a set of events
static void handleTooth(InjectionEvent* events, size_t count, const EnginePhaseInfo& phase) {
	floatus_t oneDegreeUs = getEngineRotationState()->getOneDegreeUs();
	if (std::isnan(oneDegreeUs) || std::isnan(getEngineState()->injectionOffset)) {
		// Not enough is known yet to place an injection
		return;
	}

	float leadFraction = getInjectionLeadFraction();

	// Which events have a pulse due on this tooth
	CycleEventInstance instances[MAX_CYLINDER_COUNT];
	bool isDue[MAX_CYLINDER_COUNT] = {};
	for (size_t i = 0; i < count; i++) {
		if (auto due = events[i].pollDue(phase, leadFraction, oneDegreeUs)) {
			instances[i] = due.Value;
			isDue[i] = true;
		}
	}

	// Skips first: an event that skips chooses its injection mode for the next pulse straight away, and
	// whether other events' batch pulses include its injector depends on that (see
	// shouldFireInjectorAtIndex). Deciding all of them before any pulse is put together means every pulse
	// on this tooth sees the same modes.
	for (size_t i = 0; i < count; i++) {
		if (isDue[i] && events[i].skipIfNotToInject(instances[i], phase)) {
			isDue[i] = false;
		}
	}

	// A pulse can still fail to be scheduled. Its event chooses its next injection mode only once every
	// pulse on this tooth has been put together, for the same reason.
	bool failed[MAX_CYLINDER_COUNT] = {};
	for (size_t i = 0; i < count; i++) {
		if (isDue[i]) {
			failed[i] = !events[i].injectInstance(instances[i], phase, leadFraction, oneDegreeUs);
		}
	}

	for (size_t i = 0; i < count; i++) {
		if (failed[i]) {
			events[i].update();
		}
	}
}

#if EFI_UNIT_TEST
void InjectionEvent::onTriggerTooth(const EnginePhaseInfo& phase) {
	handleTooth(this, 1, phase);
}
#endif // EFI_UNIT_TEST

void FuelSchedule::onTriggerTooth(const EnginePhaseInfo& phase) {
	// Wait for schedule to be built - this happens the first time we get RPM
	if (!isReady) {
		return;
	}

	handleTooth(elements, engine->engineState.cylinderCount, phase);
}

#endif // EFI_ENGINE_CONTROL
