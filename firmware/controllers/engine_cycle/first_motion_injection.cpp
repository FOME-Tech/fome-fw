/**
 * @file first_motion_injection.cpp
 *
 * Normally the first cranking injection waits for trigger sync and then for the next injection event's
 * angle, which can be well over a revolution after the engine starts to turn. Intake strokes in that
 * time draw no fuel. With this enabled, the first cranking cycle's worth of fuel goes in on every
 * injector at once a few teeth after the engine starts to move, so it's waiting in the ports for those
 * intakes. The scheduled cranking injections then carry on from the second cranking cycle.
 */

#include "pch.h"

#include "first_motion_injection.h"
#include "fuel_math.h"
#include "speed_density_base.h"

#if EFI_ENGINE_CONTROL

void FirstMotionInjection::onTriggerTooth() {
	if (!engineConfiguration->crankingFuelOnFirstMotion || m_tried) {
		return;
	}

	// Only when starting from a standstill
	if (!engine->rpmCalculator.isStopped()) {
		return;
	}

	m_teethSinceStandstill++;
	if (m_teethSinceStandstill < teethBeforePulse) {
		return;
	}

	m_tried = true;
	fire();
}

void FirstMotionInjection::fire() {
	// Respect fuel cuts, including flood clear
	if (!getLimpManager()->allowInjection().value) {
		return;
	}

	if (m_fuelMass <= 0) {
		return;
	}

	floatms_t durationMs = engine->module<InjectorModelPrimary>()->getInjectionDuration(m_fuelMass);
	if (!(durationMs > 0)) {
		return;
	}

	InjectorContext ctx;
	ctx.outputsMask = (1 << engine->engineState.cylinderCount) - 1;

	efitick_t startTime = getTimeNowNt();

	// Open and close are scheduled together or not at all, so the injectors can't be left open
	TimedAction actions[] = {
			{startTime, {&startInjection, ctx}},
			{startTime + (uint32_t)MSF2NT(durationMs), {&endInjection, ctx}},
	};

	if (getScheduler()->scheduleTogether("first motion inj", actions, efi::size(actions))) {
		m_fired = true;
	}
}

mass_t FirstMotionInjection::computeFuelMass() const {
	float baseFuel = 0;

	if (engineConfiguration->useRunningMathForCranking) {
		// With the engine not turning there's no airmass yet. The intake manifold is at atmospheric
		// pressure, so use a cylinder filled at that.
		float baro = Sensor::get(SensorType::BarometricPressure).value_or(101.325f);
		float iat = Sensor::get(SensorType::Iat).value_or(20);
		float cylDisplacement = engineConfiguration->displacement / engine->engineState.cylinderCount;
		mass_t airmass = idealGasLaw(cylDisplacement, baro, 273.15f + iat);

		baseFuel = engine->fuelComputer.getCycleFuel(airmass, 0, 100) * engineConfiguration->globalFuelCorrection;
	}

	// The first cranking cycle
	return getCrankingFuel3(baseFuel, 1);
}

void FirstMotionInjection::onFastCallback() {
	// Once the engine has stood still, the next time it moves is a new start
	if (!engine->triggerCentral.engineMovedRecently()) {
		m_teethSinceStandstill = 0;
		m_tried = false;
		m_fired = false;
	}

	// Work out the pulse ahead of time, so that the trigger handler only has to fire it
	if (engineConfiguration->crankingFuelOnFirstMotion && engine->rpmCalculator.isStopped()) {
		m_fuelMass = computeFuelMass();
	}

	engine->outputChannels.crankingFirstMotionPulse = m_fired;
}

#endif // EFI_ENGINE_CONTROL
