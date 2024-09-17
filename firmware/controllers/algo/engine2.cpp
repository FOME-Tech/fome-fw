/*
 * engine2.cpp
 *
 * @date Jan 5, 2019
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

// todo: move this code to more proper locations

#include "pch.h"


#include "speed_density.h"
#include "fuel_math.h"
#include "advance_map.h"
#include "closed_loop_fuel.h"
#include "launch_control.h"
#include "injector_model.h"
#include "tunerstudio.h"
#include "gitversion.h"

#if ! EFI_UNIT_TEST
#include "status_loop.h"
#endif

WarningCodeState::WarningCodeState() {
	clear();
}

void WarningCodeState::clear() {
	warningCounter = 0;
	lastErrorCode = ObdCode::None;
	recentWarnings.clear();
}

void WarningCodeState::addWarningCode(ObdCode code) {
	warningCounter++;
	lastErrorCode = code;

	warning_t* existing = recentWarnings.find(code);

	if (!existing) {
		chibios_rt::CriticalSectionLocker csl;

		// Add the code to the list
		existing = recentWarnings.add(warning_t(code));
	}

	if (existing) {
		// Reset the timer on the code to now
		existing->LastTriggered.reset();
	}

	// Reset the "any warning" timer too
	timeSinceLastWarning.reset();
}

/**
 * @param forIndicator if we want to retrieving value for TS indicator, this case a minimal period is applued
 */
bool WarningCodeState::isWarningNow() const {
	int period = maxI(3, engineConfiguration->warningPeriod);

	return !timeSinceLastWarning.hasElapsedSec(period);
}

// Check whether a particular warning is active
bool WarningCodeState::isWarningNow(ObdCode code) const {
	warning_t* warn = recentWarnings.find(code);

	// No warning found at all
	if (!warn) {
		return false;
	}

	// If the warning is old, it is not active
	return !warn->LastTriggered.hasElapsedSec(maxI(3, engineConfiguration->warningPeriod));
}

EngineState::EngineState() {
	timeSinceLastTChargeK.reset(getTimeNowNt());
}

void EngineState::updateSlowSensors() {
}

void EngineState::periodicFastCallback() {
	ScopePerf perf(PE::EngineStatePeriodicFastCallback);

#if EFI_ENGINE_CONTROL
	if (!engine->slowCallBackWasInvoked) {
		warning(ObdCode::CUSTOM_SLOW_NOT_INVOKED, "Slow not invoked yet");
	}
	efitick_t nowNt = getTimeNowNt();
	
	if (engine->rpmCalculator.isCranking()) {
		crankingTimer.reset(nowNt);
	}

	engine->fuelComputer.running.timeSinceCrankingInSecs = crankingTimer.getElapsedSeconds(nowNt);

	int rpm = Sensor::getOrZero(SensorType::Rpm);
	engine->ignitionState.sparkDwell = engine->ignitionState.getSparkDwell(rpm);
	engine->ignitionState.dwellAngle = std::isnan(rpm) ? NAN :  engine->ignitionState.sparkDwell / getOneDegreeTimeMs(rpm);

	// todo: move this into slow callback, no reason for IAT corr to be here
	engine->fuelComputer.running.intakeTemperatureCoefficient = getIatFuelCorrection();
	// todo: move this into slow callback, no reason for CLT corr to be here
	engine->fuelComputer.running.coolantTemperatureCoefficient = getCltFuelCorrection();

	engine->module<DfcoController>()->update();

	// post-cranking fuel enrichment.
	if (engineConfiguration->postCrankingFuelUseTable) {
		float postCrankingCorr = interpolate3d(
				config->postCrankingEnrichTable,
				config->postCrankingEnrichTempBins, Sensor::getOrZero(SensorType::Clt),
				config->postCrankingEnrichRuntimeBins, engine->fuelComputer.running.timeSinceCrankingInSecs
			);

		engine->fuelComputer.running.postCrankingFuelCorrection = clampF(1, postCrankingCorr, 5);
	} else {
		// for compatibility reasons, apply only if the factor is greater than unity (only allow adding fuel)
		if (engineConfiguration->postCrankingFactor > 1.0f) {
			// use interpolation for correction taper
			engine->fuelComputer.running.postCrankingFuelCorrection = interpolateClamped(0.0f, engineConfiguration->postCrankingFactor,
				engineConfiguration->postCrankingDurationSec, 1.0f, engine->fuelComputer.running.timeSinceCrankingInSecs);
		} else {
			engine->fuelComputer.running.postCrankingFuelCorrection = 1.0f;
		}
	}

	engine->ignitionState.cltTimingCorrection = getCltTimingCorrection();

	baroCorrection = getBaroCorrection();

	auto tps = Sensor::get(SensorType::Tps1);
	updateTChargeK(rpm, tps.value_or(0));

	float untrimmedInjectionMass = getInjectionMass(rpm) * engine->engineState.lua.fuelMult + engine->engineState.lua.fuelAdd;
	auto clResult = fuelClosedLoopCorrection();

	injectionStage2Fraction = getStage2InjectionFraction(rpm, engine->fuelComputer.afrTableYAxis);
	float stage2InjectionMass = untrimmedInjectionMass * injectionStage2Fraction;
	float stage1InjectionMass = untrimmedInjectionMass - stage2InjectionMass;

	// Store the pre-wall wetting injection duration for scheduling purposes only, not the actual injection duration
	engine->engineState.injectionDuration = engine->module<InjectorModelPrimary>()->getInjectionDuration(stage1InjectionMass);
	engine->engineState.injectionDurationStage2 =
		engineConfiguration->enableStagedInjection
		? engine->module<InjectorModelSecondary>()->getInjectionDuration(stage2InjectionMass)
		: 0;

	float fuelLoad = getFuelingLoad();
	injectionOffset = getInjectionOffset(rpm, fuelLoad);
	engine->lambdaMonitor.update(rpm, fuelLoad);

	float advance = getAdvance(rpm, ignitionLoad) * engine->ignitionState.luaTimingMult + engine->ignitionState.luaTimingAdd;

	// that's weird logic. also seems broken for two stroke?
	engine->outputChannels.ignitionAdvance = (float)(advance > FOUR_STROKE_CYCLE_DURATION / 2 ? advance - FOUR_STROKE_CYCLE_DURATION : advance);

	// compute per-bank fueling
	for (size_t i = 0; i < STFT_BANK_COUNT; i++) {
		float corr = clResult.banks[i];
		engine->stftCorrection[i] = corr;
	}

	// Now apply that to per-cylinder fueling and timing
	for (size_t i = 0; i < engineConfiguration->cylindersCount; i++) {
		uint8_t bankIndex = engineConfiguration->cylinderBankSelect[i];
		auto bankTrim = engine->stftCorrection[bankIndex];
		auto cylinderTrim = getCylinderFuelTrim(i, rpm, fuelLoad);

		// Apply both per-bank and per-cylinder trims
		engine->engineState.injectionMass[i] = untrimmedInjectionMass * bankTrim * cylinderTrim;

		timingAdvance[i] = advance + getCylinderIgnitionTrim(i, rpm, ignitionLoad);
	}

	shouldUpdateInjectionTiming = getInjectorDutyCycle(rpm) < 90;

	// TODO: calculate me from a table!
	trailingSparkAngle = engineConfiguration->trailingSparkAngle;

	multispark.count = getMultiSparkCount(rpm);

#if EFI_LAUNCH_CONTROL
	engine->launchController.update();
#endif //EFI_LAUNCH_CONTROL

#if EFI_ANTILAG_SYSTEM
	engine->antilagController.update();
#endif //EFI_ANTILAG_SYSTEM
#endif // EFI_ENGINE_CONTROL
}

void EngineState::updateTChargeK(int rpm, float tps) {
#if EFI_ENGINE_CONTROL
	float newTCharge = engine->fuelComputer.getTCharge(rpm, tps);
	if (!std::isnan(newTCharge)) {
		// control the rate of change or just fill with the initial value
		efitick_t nowNt = getTimeNowNt();
		float secsPassed = timeSinceLastTChargeK.getElapsedSeconds(nowNt);
		sd.tCharge = (sd.tChargeK == 0) ? newTCharge : limitRateOfChange(newTCharge, sd.tCharge, engineConfiguration->tChargeAirIncrLimit, engineConfiguration->tChargeAirDecrLimit, secsPassed);
		sd.tChargeK = convertCelsiusToKelvin(sd.tCharge);
		timeSinceLastTChargeK.reset(nowNt);
	}
#endif
}

void TriggerConfiguration::update() {
	VerboseTriggerSynchDetails = isVerboseTriggerSynchDetails();
	TriggerType = getType();
}

trigger_config_s PrimaryTriggerConfiguration::getType() const {
	return engineConfiguration->trigger;
}

bool PrimaryTriggerConfiguration::isVerboseTriggerSynchDetails() const {
	return engineConfiguration->verboseTriggerSynchDetails;
}

trigger_config_s VvtTriggerConfiguration::getType() const {
	// Convert from VVT type to trigger_config_s
	return { getVvtTriggerType(engineConfiguration->vvtMode[m_index]), 0, 0 };
}

bool VvtTriggerConfiguration::isVerboseTriggerSynchDetails() const {
	return engineConfiguration->verboseVVTDecoding;
}

bool isLockedFromUser() {
	int lock = engineConfiguration->tuneHidingKey;
	bool isLocked = lock > 0;
	if (isLocked) {
		firmwareError(ObdCode::OBD_PCM_Processor_Fault, "password protected");
	}
	return isLocked;
}

void unlockEcu(int password) {
	if (password != engineConfiguration->tuneHidingKey) {
		efiPrintf("Nope rebooting...");
#if EFI_PROD_CODE
		scheduleReboot();
#endif // EFI_PROD_CODE
	} else {
		efiPrintf("Unlocked! Burning...");
		engineConfiguration->tuneHidingKey = 0;
		requestBurn();
	}
}
