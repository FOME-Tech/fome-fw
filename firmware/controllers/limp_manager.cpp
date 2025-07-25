#include "pch.h"

#include "limp_manager.h"
#include "fuel_math.h"
#include "main_trigger_callback.h"

#define CLEANUP_MODE_TPS 90

static bool noFiringUntilVvtSync(vvt_mode_e vvtMode) {
	auto operationMode = getEngineRotationState()->getOperationMode();

	// V-Twin MAP phase sense needs to always wait for sync
	if (vvtMode == VVT_MAP_V_TWIN) {
		return true;
	}

	if (engineConfiguration->isPhaseSyncRequiredForIgnition) {
		// in rare cases engines do not like random sequential mode
		return true;
	}

	// Symmetrical crank modes require cam sync before firing
	// non-symmetrical cranks can use faster spin-up mode (firing in wasted/batch before VVT sync)
	// Examples include Nissan MR/VQ, Miata NB, etc
	return
		operationMode == FOUR_STROKE_SYMMETRICAL_CRANK_SENSOR ||
		operationMode == FOUR_STROKE_THREE_TIMES_CRANK_SENSOR ||
		operationMode == FOUR_STROKE_TWELVE_TIMES_CRANK_SENSOR;
}

void LimpManager::onFastCallback() {
	updateState(Sensor::getOrZero(SensorType::Rpm), getTimeNowNt());
}

void LimpManager::updateState(float rpm, efitick_t nowNt) {
	Clearable allowFuel = engineConfiguration->isInjectionEnabled;
	Clearable allowSpark = engineConfiguration->isIgnitionEnabled;

#if !EFI_UNIT_TEST && EFI_SHAFT_POSITION_INPUT
	// if the ignition is off, cut fuel + spark
	// UNLESS we're self stimulating, so you can test the ECU on the bench on USB
	if (!m_ignitionOn && !engine->triggerCentral.directSelfStimulation) {
		allowFuel.clear(ClearReason::IgnitionOff);
		allowSpark.clear(ClearReason::IgnitionOff);
	}
#endif

	if (engine->engineState.lua.luaIgnCut) {
		allowSpark.clear(ClearReason::Lua);
	}

	// Don't inject fuel during Harley compression release - it sprays fuel everywhere
	if (engine->module<HarleyAcr>()->isActive()) {
		allowFuel.clear(ClearReason::ACR);
	}

	// User-configured hard RPM limit, either constant or CLT-lookup
	m_hardRevLimit = engineConfiguration->useCltBasedRpmLimit
		? interpolate2d(Sensor::getOrZero(SensorType::Clt), config->cltRevLimitRpmBins, config->cltRevLimitRpm)
		: (float)engineConfiguration->rpmHardLimit;

	if (isHardRevLimit(rpm)) {
		if (engineConfiguration->cutFuelOnHardLimit) {
			allowFuel.clear(ClearReason::HardLimit);
		}

		if (engineConfiguration->cutSparkOnHardLimit) {
			allowSpark.clear(ClearReason::HardLimit);
		}
	}

	if (engine->lambdaMonitor.isCut()) {
		allowFuel.clear(ClearReason::LambdaProtection);
	}

#if EFI_SHAFT_POSITION_INPUT
	if (noFiringUntilVvtSync(engineConfiguration->vvtMode[0])
			&& !engine->triggerCentral.triggerState.hasSynchronizedPhase()) {
		// Any engine that requires cam-assistance for a full crank sync (symmetrical crank) can't schedule until we have cam sync
		// examples:
		// NB2, Nissan VQ/MR: symmetrical crank wheel and we need to make sure no spark happens out of sync
		// VTwin Harley: uneven firing order, so we need "cam" MAP sync to make sure no spark happens out of sync
		allowFuel.clear(ClearReason::EnginePhase);
		allowSpark.clear(ClearReason::EnginePhase);
	}
#endif // EFI_SHAFT_POSITION_INPUT

	// Force fuel limiting on the fault rev limit
	if (rpm > m_faultRevLimit) {
		allowFuel.clear(ClearReason::FaultRevLimit);
	}

	// Limit fuel only on boost pressure (limiting spark bends valves)
	float mapCut = engineConfiguration->boostCutPressure;
	if (mapCut != 0) {
		// require drop of 20kPa to resume fuel
		if (m_boostCutHysteresis.test(Sensor::getOrZero(SensorType::Map), mapCut, mapCut - 20)) {
			allowFuel.clear(ClearReason::BoostCut);
		}
	}
#if EFI_SHAFT_POSITION_INPUT
	if (engine->rpmCalculator.isRunning()) {
		bool hasOilpSensor = Sensor::hasSensor(SensorType::OilPressure);
		auto oilp = Sensor::get(SensorType::OilPressure);
		uint16_t minOilPressure = engineConfiguration->minOilPressureAfterStart;

		// Only check if the setting is enabled and you have an oil pressure sensor
		if (minOilPressure > 0 && hasOilpSensor) {
			// Has it been long enough we should have pressure?
			bool isTimedOut = engine->rpmCalculator.getSecondsSinceEngineStart(nowNt) > 5.0f;

			// Only check before timed out
			if (!isTimedOut) {
				if (oilp) {
					// We had oil pressure! Set the flag.
					if (oilp.Value > minOilPressure) {
						m_hadOilPressureAfterStart = true;
					}
				}
			}

			// If time is up, the sensor works, and no pressure, kill the engine.
			if (isTimedOut && !m_hadOilPressureAfterStart) {
				allowFuel.clear(ClearReason::OilPressure);
			}
		}

		if (engineConfiguration->enableOilPressureProtect) {
			bool isPressureSufficient;
			if (engineConfiguration->useOilPressureSwitch) {
				isPressureSufficient = getOilSwitchState();
			} else {
				if (oilp) {
					float minPressure = interpolate2d(
											rpm,
											config->minimumOilPressureBins,
											config->minimumOilPressureValues
										);
					isPressureSufficient = oilp.Value > minPressure;
				} else {
					// The sensor is dead, assume things are fine
					// (sensor error is separately handled)
					isPressureSufficient = true;
				}
			}

			if (isPressureSufficient) {
				m_lowOilPressureTimer.reset(nowNt);
			}

			if (m_lowOilPressureTimer.hasElapsedSec(engineConfiguration->minimumOilPressureTimeout)) {
				allowFuel.clear(ClearReason::OilPressure);
			}
		}
	} else {
		// reset state in case of stalled engine
		m_hadOilPressureAfterStart = false;
		m_lowOilPressureTimer.reset(nowNt);
	}

	// If we're in engine stop mode, inhibit fuel
	if (shutdownController.isEngineStop(nowNt)) {
		/**
		 * todo: we need explicit clarification on why do we cut fuel but do not cut spark here!
		 */
		allowFuel.clear(ClearReason::StopRequested);
	}

	{
		// If duty cycle is high, impose a fuel cut rev limiter.
		// This is safer than attempting to limp along with injectors or a pump that are out of flow.
		// Two conditions will trigger a cut:
		// - An instantaneous excursion above maxInjectorDutyInstant
		// - A sustained excursion above maxInjectorDutySustained for a duration of >= maxInjectorDutySustainedTimeout
		// Only reset once below 20% duty to force the driver to lift off the pedal

		auto injDutyCycle = getInjectorDutyCycle(rpm);
		bool isOverInstantDutyCycle = injDutyCycle > engineConfiguration->maxInjectorDutyInstant;
		bool isOverSustainedDutyCycle = injDutyCycle > engineConfiguration->maxInjectorDutySustained;
		bool isUnderLowDuty = injDutyCycle < 20;

		if (!isOverSustainedDutyCycle) {
			// Duty cycle is OK, reset timer.
			m_injectorDutySustainedTimer.reset(nowNt);
		}

		// True if isOverSustainedDutyCycle has been true for longer than the timeout
		bool sustainedLimitTimedOut = m_injectorDutySustainedTimer.hasElapsedSec(engineConfiguration->maxInjectorDutySustainedTimeout);

		bool someLimitTripped = isOverInstantDutyCycle || sustainedLimitTimedOut;

		if (m_injectorDutyCutHysteresis.test(someLimitTripped, isUnderLowDuty)) {
			allowFuel.clear(ClearReason::InjectorDutyCycle);
			warning(ObdCode::CUSTOM_TOO_LONG_FUEL_INJECTION, "Injector duty cycle cut %.1f", injDutyCycle);
		}
	}

	// If the pedal is pushed while not running, cut fuel to clear a flood condition.
	if (!engine->rpmCalculator.isRunning() &&
		engineConfiguration->isCylinderCleanupEnabled &&
		Sensor::getOrZero(SensorType::DriverThrottleIntent) > CLEANUP_MODE_TPS) {
		allowFuel.clear(ClearReason::FloodClear);
	}

	if (!engine->isMainRelayEnabled()) {
/*
todo AndreiKA this change breaks 22 unit tests?
		allowFuel.clear();
		allowSpark.clear();
*/
	}
	
#endif // EFI_SHAFT_POSITION_INPUT

#if EFI_LAUNCH_CONTROL
	// Fuel cut if launch control engaged
	if (engine->launchController.isLaunchFuelRpmRetardCondition()) {
		allowFuel.clear(ClearReason::LaunchCut);
	}

	// Spark cut if launch control engaged
	if (engine->launchController.isLaunchSparkRpmRetardCondition()) {
		allowSpark.clear(ClearReason::LaunchCut);
	}
#endif // EFI_LAUNCH_CONTROL

	m_transientAllowInjection = allowFuel;
	m_transientAllowIgnition = allowSpark;

	if (!m_transientAllowInjection || !m_transientAllowIgnition) {
		// Tracks the last time any cut happened
		m_lastCutTime.reset(nowNt);
	}

	// Update output channels
	engine->outputChannels.fuelCutReason = static_cast<uint8_t>(allowInjection().reason);
	engine->outputChannels.sparkCutReason = static_cast<uint8_t>(allowIgnition().reason);
}

bool LimpManager::isHardRevLimit(float rpm) {
	// Configurable hysteresis for how far to drop before resuming
	float resumeRpm = m_hardRevLimit - engineConfiguration->rpmHardLimitHyst;

	return m_revLimitHysteresis.test(rpm, m_hardRevLimit, resumeRpm);
}

void LimpManager::onIgnitionStateChanged(bool ignitionOn) {
	m_ignitionOn = ignitionOn;
}

void LimpManager::reportEtbProblem() {
	m_allowEtb.clear(ClearReason::EtbProblem);
	setFaultRevLimit(/*rpm*/1500);
}

void LimpManager::fatalError() {
	m_allowEtb.clear(ClearReason::Fatal);
	m_allowIgnition.clear(ClearReason::Fatal);
	m_allowInjection.clear(ClearReason::Fatal);
	m_allowTriggerInput.clear(ClearReason::Fatal);

	setFaultRevLimit(/*rpm*/0);
}

void LimpManager::setFaultRevLimit(int limit) {
	// Only allow decreasing the limit
	// aka uses the limit of the worst fault to yet occur
	m_faultRevLimit = minI(m_faultRevLimit, limit);
}

bool LimpManager::allowElectronicThrottle() const {
	return m_allowEtb;
}

bool LimpManager::allowTriggerInput() const {
	return m_allowTriggerInput;
}

LimpState LimpManager::allowInjection() const {
	if (!m_allowInjection) {
		return {false, m_allowInjection.why()};
	}
	if (!m_transientAllowInjection) {
		return {false, m_transientAllowInjection.why()};
	}
	return {true, ClearReason::None};
}

LimpState LimpManager::allowIgnition() const {
	if (!m_allowIgnition) {
		return {false, m_allowIgnition.why()};
	}
	if (!m_transientAllowIgnition) {
		return {false, m_transientAllowIgnition.why()};
	}
	return {true, ClearReason::None};
}

float LimpManager::getTimeSinceAnyCut() const {
	return m_lastCutTime.getElapsedSeconds();
}
