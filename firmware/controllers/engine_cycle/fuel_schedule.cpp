/**
 * @file fuel_schedule.cpp
 *
 * Handles injection scheduling
 */

#include "pch.h"
#include "fuel_math.h"

#if EFI_ENGINE_CONTROL

extern bool printFuelDebug;

void startInjection(InjectorContext ctx) {
	efitick_t nowNt = getTimeNowNt();

	uint16_t mask = ctx.outputsMask;
	size_t idx = 0;

	while(mask) {
		if (mask & 0x1) {
			enginePins.injectors[idx].open(nowNt);

			if (ctx.stage2Active) {
				enginePins.injectorsStage2[idx].open(nowNt);
			}
		}

		mask = mask >> 1;
		idx++;
	}
}

void endInjection(InjectorContext ctx) {
	efitick_t nowNt = getTimeNowNt();

	uint16_t mask = ctx.outputsMask;
	size_t idx = 0;

	while(mask) {
		if (mask & 0x1) {
			enginePins.injectors[idx].close(nowNt);
		}

		mask = mask >> 1;
		idx++;
	}

	if (ctx.splitDurationUs > 0) {
		efitick_t openTime = getTimeNowNt() + MS2NT(2);
		efitick_t closeTime = openTime + US2NT(ctx.splitDurationUs);

		// Zero out the split duration so it doesn't repeat
		ctx.splitDurationUs = 0;

		getScheduler()->schedule("split inj", nullptr, openTime, { &startInjection, ctx });
		getScheduler()->schedule("split inj", nullptr, closeTime, { endInjection, ctx });
	} else {
		// No splits remaining, prepare for next cycle
		if (ctx.eventIndex < efi::size(getFuelSchedule()->elements)) {
			getFuelSchedule()->elements[ctx.eventIndex].update();
		}
	}
}

void endInjectionStage2(InjectorContext ctx) {
	efitick_t nowNt = getTimeNowNt();

	uint16_t mask = ctx.outputsMask;
	size_t idx = 0;

	while(mask) {
		if (mask & 0x1) {
			enginePins.injectorsStage2[idx].close(nowNt);
		}

		mask = mask >> 1;
		idx++;
	}
}

uint16_t InjectionEvent::calculateInjectorOutputMask() const {
	uint16_t mask = 0;

	switch (m_injectionMode) {
		case IM_SIMULTANEOUS:
			// Simultaneous mode fires all injectors
			mask = (1 << engineConfiguration->cylindersCount) - 1;
			break;
		case IM_SINGLE_POINT:
			// Single point only fires injector 1
			mask = 1;
			break;
		case IM_BATCH:
			// In batch mode, also fire the cylinder 360 degrees out to support "two-wire batch" mode

			// Compute the position of this cylinder's twin in the firing order
			// Each injector gets fired as a primary (the same as sequential), but also
			// fires the injector 360 degrees later in the firing order.
			mask |= (1 << getCylinderNumberAtIndex((ownIndex + (engineConfiguration->cylindersCount / 2)) % engineConfiguration->cylindersCount));

			// falls through
		case IM_SEQUENTIAL:
			// In batch+sequential, fire this cylinder's injector
			mask |= 1 << cylinderNumber;
			break;
	}

	return mask;
}

void InjectionEvent::onTriggerTooth(efitick_t nowNt, float currentPhase, float nextPhase) {
	auto eventAngle = injectionStartAngle;

	// Determine whether our angle is going to happen before (or near) the next tooth
	if (!isPhaseInRange(eventAngle, currentPhase, nextPhase)) {
		return;
	}

	// don't allow split inj in simultaneous mode
	// TODO: #364 implement logic to actually enable split injections
	bool doSplitInjection = false && !isSimultaneous;

	// Select fuel mass from the correct cylinder
	auto cycleMassGrams = engine->cylinders[this->cylinderNumber].getInjectionMass();

	float injectionMassGrams = cycleMassGrams * getInjectionModeDurationMultiplier(m_injectionMode);

	// Perform wall wetting adjustment on fuel mass, not duration, so that
	// it's correct during fuel pressure (injector flow) or battery voltage (deadtime) transients
	// TODO: is it correct to wall wet on both pulses?
	injectionMassGrams = wallFuel.adjust(injectionMassGrams);

	// Disable staging in simultaneous mode or split injection mode
	float stage2Fraction = (isSimultaneous || doSplitInjection) ? 0 : getEngineState()->injectionStage2Fraction;

	// Compute fraction of fuel on stage 2, remainder goes on stage 1
	const float injectionMassStage2 = stage2Fraction * injectionMassGrams;
	float injectionMassStage1 = injectionMassGrams - injectionMassStage2;

	{
		// Log this fuel as consumed

		#ifdef MODULE_TRIP_ODO
		int numberOfInjections = getNumberOfInjections(m_injectionMode);

		float actualInjectedMass = numberOfInjections * (injectionMassStage1 + injectionMassStage2);

		engine->module<TripOdometer>()->consumeFuel(actualInjectedMass, nowNt);
		#endif // MODULE_TRIP_ODO
	}

	if (doSplitInjection) {
		// If in split mode, do the injection in two halves
		injectionMassStage1 = injectionMassStage1 / 2;
	}

	const floatms_t injectionDurationStage1 = engine->module<InjectorModelPrimary>()->getInjectionDuration(injectionMassStage1);
	const floatms_t injectionDurationStage2 = injectionMassStage2 > 0 ? engine->module<InjectorModelSecondary>()->getInjectionDuration(injectionMassStage2) : 0;

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
		return;
	}
	if (injectionDurationStage1 < 0) {
		warning(ObdCode::CUSTOM_OBD_NEG_INJECTION, "Negative injection pulse %.2f", injectionDurationStage1);
		return;
	}

	// If somebody commanded an impossibly short injection, do nothing.
	// Durations under 50us-ish aren't safe for the scheduler
	// as their order may be swapped, resulting in a stuck open injector
	// see https://github.com/rusefi/rusefi/pull/596 for more details
	if (injectionDurationStage1 < 0.050f)
	{
		return;
	}

	floatus_t durationUsStage1 = MS2US(injectionDurationStage1);
	floatus_t durationUsStage2 = MS2US(injectionDurationStage2);

	// Only bother with the second stage if it's long enough to be relevant
	bool hasStage2Injection = durationUsStage2 > 50;

	InjectorContext ctx;
	ctx.eventIndex = ownIndex;
	ctx.stage2Active = hasStage2Injection;
	ctx.outputsMask = calculateInjectorOutputMask();

#if EFI_PRINTF_FUEL_DETAILS
if (printFuelDebug) {
	printf("handleFuelInjectionEvent fuelout %06x injection_duration %dus engineCycleDuration=%.1fms\t\n", ctx.outputsMask, (int)durationUsStage1,
			(int)MS2US(getCrankshaftRevolutionTimeMs(Sensor::getOrZero(SensorType::Rpm))) / 1000.0);
}
#endif /*EFI_PRINTF_FUEL_DETAILS */

	if (doSplitInjection) {
		ctx.splitDurationUs = durationUsStage1;
	}

	// Correctly wrap injection start angle
	float angleFromNow = eventAngle - currentPhase;
	if (angleFromNow < 0) {
		angleFromNow += getEngineState()->engineCycle;
	}

	// Schedule opening (stage 1 + stage 2 open together)
	efitick_t startTime = scheduleByAngle(nullptr, nowNt, angleFromNow, { &startInjection, ctx });

	// Schedule closing stage 1
	efidur_t durationStage1Nt = US2NT((int)durationUsStage1);
	efitick_t turnOffTimeStage1 = startTime + durationStage1Nt;

	getScheduler()->schedule("inj", nullptr, turnOffTimeStage1, { &endInjection, ctx });

	// Schedule closing stage 2 (if applicable)
	if (hasStage2Injection) {
		efitick_t turnOffTimeStage2 = startTime + US2NT((int)durationUsStage2);
		getScheduler()->schedule("inj stage 2", nullptr, turnOffTimeStage2, { &endInjectionStage2, ctx });
	}

#if EFI_UNIT_TEST
	printf("scheduling injection angle=%.2f/delay=%d injectionDuration=%d %d\r\n", angleFromNow, (int)NT2US(startTime - nowNt), (int)durationUsStage1, (int)durationUsStage2);
#endif
#if EFI_DEFAILED_LOGGING
	efiPrintf("handleFuel pin=%s eventIndex %d duration=%.2fms %d", outputs[0]->name,
			injEventIndex,
			injectionDurationStage1,
			getRevolutionCounter());
	efiPrintf("handleFuel pin=%s delay=%.2f %d", outputs[0]->name, NT2US(startTime - nowNt),
			getRevolutionCounter());
#endif /* EFI_DEFAILED_LOGGING */
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

// Determines how much to adjust injection opening angle based on the injection's duration and the current phasing mode
static float getInjectionAngleCorrection(float fuelMs, float oneDegreeUs) {
	auto mode = engineConfiguration->injectionTimingMode;
	if (mode == InjectionTimingMode::Start) {
		// Start of injection gets no correction for duration
		return 0;
	}

	efiAssert(ObdCode::CUSTOM_ERR_ASSERT, !std::isnan(fuelMs), "NaN fuelMs", false);

	angle_t injectionDurationAngle = MS2US(fuelMs) / oneDegreeUs;
	efiAssert(ObdCode::CUSTOM_ERR_ASSERT, !std::isnan(injectionDurationAngle), "NaN injectionDurationAngle", false);
	assertAngleRange(injectionDurationAngle, "injectionDuration_r", ObdCode::CUSTOM_INJ_DURATION);

	if (mode == InjectionTimingMode::Center) {
		// Center of injection is half-corrected for duration
		return injectionDurationAngle * 0.5f;
	} else {
		// End of injection gets "full correction" so we advance opening by the full duration
		return injectionDurationAngle;
	}
}

// Returns the start angle of this injector in engine coordinates (0-720 for a 4 stroke),
// or unexpected if unable to calculate the start angle due to missing information.
expected<angle_t> OneCylinder::computeInjectionAngle(injection_mode_e mode) const {
	floatus_t oneDegreeUs = getEngineRotationState()->getOneDegreeUs();
	if (std::isnan(oneDegreeUs)) {
		// in order to have fuel schedule we need to have current RPM
		return unexpected;
	}

	// injection phase may be scheduled by injection end, so we need to step the angle back
	// for the duration of the injection
	angle_t injectionDurationAngle = getInjectionAngleCorrection(getEngineState()->injectionDuration, oneDegreeUs);

	// User configured offset - degrees after TDC combustion
	floatus_t injectionOffset = getEngineState()->injectionOffset;
	if (std::isnan(injectionOffset)) {
		// injection offset map not ready - we are not ready to schedule fuel events
		return unexpected;
	}

	angle_t openingAngle = injectionOffset - injectionDurationAngle;
	assertAngleRange(openingAngle, "openingAngle_r", ObdCode::CUSTOM_ERR_6554);
	wrapAngle(openingAngle, "addFuel#1", ObdCode::CUSTOM_ERR_6555);
	// TODO: should we log per-cylinder injection timing? #76
	getTunerStudioOutputChannels()->injectionOffset = openingAngle;

	// Convert from cylinder-relative to cylinder-1-relative
	openingAngle += getAngleOffset();

	efiAssert(ObdCode::CUSTOM_ERR_ASSERT, !std::isnan(openingAngle), "findAngle#3", false);
	assertAngleRange(openingAngle, "findAngle#a33", ObdCode::CUSTOM_ERR_6544);

	wrapAngle(openingAngle, "addFuel#2", ObdCode::CUSTOM_ERR_6555);

#if EFI_UNIT_TEST
	printf("registerInjectionEvent openingAngle=%.2f inj %d\r\n", openingAngle, m_cylinderNumber);
#endif

	return openingAngle;
}

bool InjectionEvent::updateInjectionAngle(injection_mode_e mode) {
	if (auto result = engine->cylinders[cylinderNumber].computeInjectionAngle(mode)) {
		// If injector duty cycle is high, lock injection SOI so that we
		// don't miss injections at or above 100% duty
		if (getEngineState()->shouldUpdateInjectionTiming) {
			injectionStartAngle = result.Value;
		}

		return true;
	} else {
		return false;
	}
}

/**
 * @returns false in case of error, true if success
 */
bool InjectionEvent::update() {
	cylinderNumber = getCylinderNumberAtIndex(ownIndex);

	injection_mode_e mode = getCurrentInjectionMode();

	if (updateInjectionAngle(mode)) {
		m_injectionMode = mode;

		engine->outputChannels.currentInjectionMode = static_cast<uint8_t>(mode);

		return true;
	} else {
		return false;
	}
}

void FuelSchedule::addFuelEvents() {
	for (size_t cylinderIndex = 0; cylinderIndex < engineConfiguration->cylindersCount; cylinderIndex++) {
		bool result = elements[cylinderIndex].update();

		if (!result) {
			invalidate();
			return;
		}
	}

	// We made it through all cylinders, mark the schedule as ready so it can be used
	isReady = true;
}

void FuelSchedule::onTriggerTooth(efitick_t nowNt, float currentPhase, float nextPhase) {
	// Wait for schedule to be built - this happens the first time we get RPM
	if (!isReady) {
		return;
	}

	for (size_t i = 0; i < engineConfiguration->cylindersCount; i++) {
		elements[i].onTriggerTooth(nowNt, currentPhase, nextPhase);
	}
}

#endif // EFI_ENGINE_CONTROL
