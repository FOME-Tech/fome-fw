/*
 * @file prime_injection.cpp
 */

#include "pch.h"
#include "prime_injection.h"
#include "injection_gpio.h"
#include "sensor.h"
#include "backup_ram.h"

floatms_t PrimeController::getPrimeDuration() const {
	auto clt = Sensor::get(SensorType::Clt);

	// If the coolant sensor is dead, skip the prime. The engine will still start fine, but may take a little longer.
	if (!clt) {
		return 0;
	}

	auto primeMass = 0.001f * // convert milligram to gram
					 interpolate2d(clt.Value, engineConfiguration->primeBins, engineConfiguration->primeValues);

	efiPrintf("Priming pulse mass: %.4f g", primeMass);

	return engine->module<InjectorModelPrimary>()->getInjectionDuration(primeMass);
}

// Check if the engine is not stopped or cylinder cleanup is activated
static bool isPrimeInjectionPulseSkipped() {
	// Skip if the engine is already spinning
	if (!getEngineRotationState()->isStopped()) {
		return true;
	}

	// Skip if cylinder cleanup is active
	return engineConfiguration->isCylinderCleanupEnabled && (Sensor::getOrZero(SensorType::Tps1) > CLEANUP_MODE_TPS);
}

void PrimeController::onIgnitionStateChanged(bool ignitionOn) {
	if (!ignitionOn) {
		// don't prime on ignition-off
		return;
	}

	// First, we need a protection against 'fake' ignition switch on and off (i.e. no engine started), to avoid repeated
	// prime pulses. So we check and update the ignition switch counter in non-volatile backup-RAM
	uint32_t ignSwitchCounter = getKeyCycleCounter();

	// if we're just toying with the ignition switch, give it another chance eventually...
	if (ignSwitchCounter > 10) {
		ignSwitchCounter = 0;
	}

	// If we're going to skip this pulse, then save the counter as 0.
	// That's because we'll definitely need the prime pulse next time (either due to the cylinder cleanup or the engine
	// spinning)
	if (isPrimeInjectionPulseSkipped()) {
		ignSwitchCounter = -1;
	}

	// start prime injection if this is a 'fresh start'
	if (ignSwitchCounter == 0) {
		// Give sensors long enough to wake up before priming
		constexpr float minimumPrimeDelayMs = 100;
		float delayMs = engineConfiguration->primingDelay * 1000 + minimumPrimeDelayMs;
		if (std::isfinite(delayMs) && delayMs >= 0 && delayMs < MaximumScheduleDelayUs / 1000) {
			auto startTime = getTimeNowNt() + US2NT(static_cast<int>(MS2US(delayMs)));
			ScheduledAction request{startTime, {PrimeController::onPrimeStartAdapter, this}};
			scheduleFuelCallbacks(&request, 1, true);
		}
	} else {
		efiPrintf("Skipped priming pulse since ignSwitchCounter = %lu", ignSwitchCounter);
	}

	// we'll reset it later when the engine starts
	setKeyCycleCounter(ignSwitchCounter + 1);
}

#if EFI_PROD_CODE
uint32_t PrimeController::getKeyCycleCounter() const {
	return getBackupSram()->IgnCounter;
}

void PrimeController::setKeyCycleCounter(uint32_t count) {
	getBackupSram()->IgnCounter = count;
}
#else // not EFI_PROD_CODE
uint32_t PrimeController::getKeyCycleCounter() const {
	return 0;
}

void PrimeController::setKeyCycleCounter(uint32_t) {}
#endif

void PrimeController::onPrimeStartAdapter(PrimeController* instance) {
	instance->onPrimeStart();
	engine->airmassInjectionState.callbackCompleted();
}

void PrimeController::onPrimeOpenAdapter(PrimeController* instance) {
	instance->onPrimeOpen();
	engine->airmassInjectionState.callbackCompleted();
}

void PrimeController::onPrimeEndAdapter(PrimeController* instance) {
	instance->onPrimeEnd();
	engine->airmassInjectionState.callbackCompleted();
}

void PrimeController::onPrimeStart() {
	chibios_rt::CriticalSectionLocker csl;
	// A delayed request may have been accepted under another strategy. Check at actual start.
	if (!engine->airmassInjectionState.allowPrime() || m_isPriming || isPrimeInjectionPulseSkipped()) {
		return;
	}
	auto durationMs = getPrimeDuration();
	if (!std::isfinite(durationMs) || durationMs < 0.050f || durationMs >= MaximumScheduleDelayUs / 1000) {
		return;
	}

	auto startTime = getTimeNowNt();
	auto endTime = startTime + US2NT(static_cast<int>(MS2US(durationMs)));
	if (engine->engineState.cylinderCount == 0 || engine->engineState.cylinderCount > MAX_CYLINDER_COUNT) {
		return;
	}
	m_primeOutputsMask = (1 << engine->engineState.cylinderCount) - 1;
	m_isPriming = true;
	ScheduledAction events[] = {
			{startTime, {onPrimeOpenAdapter, this}},
			{endTime, {onPrimeEndAdapter, this}},
	};
	if (!scheduleFuelCallbacks(events, efi::size(events), true)) {
		m_isPriming = false;
	}
}

void PrimeController::onPrimeOpen() {
	InjectorContext ctx;
	ctx.outputsMask = m_primeOutputsMask;
	startInjection(ctx);
}

void PrimeController::onPrimeEnd() {
	InjectorContext ctx;
	// Match the mask accepted at start even if the tune has changed since then.
	ctx.outputsMask = m_primeOutputsMask;
	endInjection(ctx);
	m_isPriming = false;
}

void PrimeController::onSlowCallback() {
	if (!getEngineRotationState()->isStopped()) {
#if EFI_PROD_CODE
		getBackupSram()->IgnCounter = 0;
#endif /* EFI_PROD_CODE */
	}
}
