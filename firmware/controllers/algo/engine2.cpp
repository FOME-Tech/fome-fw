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

void EngineState::periodicFastCallback() {
	ScopePerf perf(PE::EngineStatePeriodicFastCallback);

#if EFI_ENGINE_CONTROL
	efitick_t nowNt = getTimeNowNt();
	bool isCranking = engine->rpmCalculator.isCranking();
	float rpm = Sensor::getOrZero(SensorType::Rpm);

	if (isCranking) {
		crankingTimer.reset(nowNt);
	}

	engine->fuelComputer.running.timeSinceCrankingInSecs = crankingTimer.getElapsedSeconds(nowNt);

	engine->ignitionState.updateDwell(rpm, isCranking);

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

	baroCorrection = getBaroCorrection();

	auto tps = Sensor::get(SensorType::Tps1);
	updateTChargeK(rpm, tps.value_or(0));

	float cycleFuelMass = getCycleInjectionMass(rpm, isCranking) * engine->engineState.lua.fuelMult + engine->engineState.lua.fuelAdd;
	auto clResult = fuelClosedLoopCorrection();

	{
		float injectionFuelMass = cycleFuelMass * getInjectionModeDurationMultiplier(getCurrentInjectionMode());
		
		injectionStage2Fraction = getStage2InjectionFraction(rpm, engine->fuelComputer.afrTableYAxis);
		float stage2InjectionMass = injectionFuelMass * injectionStage2Fraction;
		float stage1InjectionMass = injectionFuelMass - stage2InjectionMass;

		// Store the pre-wall wetting injection duration for scheduling purposes only, not the actual injection duration
		engine->engineState.injectionDuration = engine->module<InjectorModelPrimary>()->getInjectionDuration(stage1InjectionMass);
		engine->engineState.injectionDurationStage2 =
			engineConfiguration->enableStagedInjection
			? engine->module<InjectorModelSecondary>()->getInjectionDuration(stage2InjectionMass)
			: 0;
	}

	float fuelLoad = getFuelingLoad();
	injectionOffset = getInjectionOffset(rpm, fuelLoad);
	engine->lambdaMonitor.update(rpm, fuelLoad);

	engine->ignitionState.updateAdvanceCorrections(ignitionLoad);
	float untrimmedAdvance = engine->ignitionState.getAdvance(rpm, ignitionLoad, isCranking)
					* engine->ignitionState.luaTimingMult + engine->ignitionState.luaTimingAdd;

	// that's weird logic. also seems broken for two stroke?
	engine->outputChannels.ignitionAdvance = (float)(untrimmedAdvance > FOUR_STROKE_CYCLE_DURATION / 2 ? untrimmedAdvance - FOUR_STROKE_CYCLE_DURATION : untrimmedAdvance);

	// compute per-bank fueling
	for (size_t i = 0; i < STFT_BANK_COUNT; i++) {
		engine->stftCorrection[i] = clResult.banks[i];
	}

	// Now apply that to per-cylinder fueling and timing
	for (size_t i = 0; i < engineConfiguration->cylindersCount; i++) {
		uint8_t bankIndex = engineConfiguration->cylinderBankSelect[i];
		auto bankTrim = engine->stftCorrection[bankIndex];
		auto cylinderTrim = getCylinderFuelTrim(i, rpm, fuelLoad);

		// Apply both per-bank and per-cylinder trims
		engine->cylinders[i].setInjectionMass(cycleFuelMass * bankTrim * cylinderTrim);

		engine->cylinders[i].setIgnitionTimingBtdc(untrimmedAdvance + getCylinderIgnitionTrim(i, rpm, ignitionLoad));
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

void EngineState::updateTChargeK(float rpm, float tps) {
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

void EngineState::updateSplitInjection() {
	if (!requestSplitInjection) {
		doSplitInjection = false;
		return;
	}

	// toggle every 2 seconds
	if (splitInjectionTimer.hasElapsedSec(2)) {
		splitInjectionTimer.reset();

		doSplitInjection ^= true;
	}
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

vvt_mode_e VvtTriggerConfiguration::getVvtMode() const {
	return engineConfiguration->vvtMode[m_index];
}

bool VvtTriggerConfiguration::needsTriggerDecoder() const {
	auto mode = getVvtMode();

	return mode != VVT_INACTIVE
			&& mode != VVT_TOYOTA_3_TOOTH
			&& mode != VVT_HONDA_K_INTAKE
			&& mode != VVT_MAP_V_TWIN
			&& mode != VVT_SINGLE_TOOTH;
}

// VVT decoding uses "normal" trigger shapes for decoding but is configured separately.
// This maps from vvt_mode_e -> trigger_type_e (for supported shapes)
static trigger_type_e getVvtTriggerType(vvt_mode_e vvtMode) {
	switch (vvtMode) {
	case VVT_INACTIVE:
		return trigger_type_e::TT_ONE;
	case VVT_MIATA_NB:
		return trigger_type_e::TT_VVT_MIATA_NB;
	case VVT_MIATA_NA:
		return trigger_type_e::TT_VVT_MIATA_NA;
	case VVT_BOSCH_QUICK_START:
		return trigger_type_e::TT_VVT_BOSCH_QUICK_START;
	case VVT_HONDA_K_EXHAUST:
		return trigger_type_e::TT_HONDA_K_CAM_4_1;
	case VVT_FORD_ST170:
		return trigger_type_e::TT_FORD_ST170;
	case VVT_BARRA_3_PLUS_1:
		return trigger_type_e::TT_VVT_BARRA_3_PLUS_1;
	case VVT_MAZDA_SKYACTIV:
		return trigger_type_e::TT_VVT_MAZDA_SKYACTIV;
	case VVT_MAZDA_L:
		return trigger_type_e::TT_VVT_MAZDA_L;
	case VVT_NISSAN_VQ:
		return trigger_type_e::TT_VVT_NISSAN_VQ35;
	case VVT_TOYOTA_4_1:
		return trigger_type_e::TT_VVT_TOYOTA_4_1;
	case VVT_MITSUBISHI_3A92:
		return trigger_type_e::TT_VVT_MITSUBISHI_3A92;
	case VVT_MITSUBISHI_6G75:
	case VVT_NISSAN_MR:
		return trigger_type_e::TT_NISSAN_MR18_CAM_VVT;
	case VVT_MITSUBISHI_4G9x:
		return trigger_type_e::TT_MITSU_4G9x_CAM;
	case VVT_MITSUBISHI_4G63:
		return trigger_type_e::TT_MITSU_4G63_CAM;
	case VVT_BMW_N63TU:
		return trigger_type_e::TT_BMW_N63TU;
	default:
		firmwareError("getVvtTriggerType for %s", getVvt_mode_e(vvtMode));
		return trigger_type_e::TT_ONE; // we have to return something for the sake of -Werror=return-type
	}
}


trigger_config_s VvtTriggerConfiguration::getType() const {
	if (!needsTriggerDecoder()) {
		return { trigger_type_e::TT_UNUSED, 0, 0 };
	}

	// Convert from VVT type to trigger_config_s
	return { getVvtTriggerType(getVvtMode()), 0, 0 };
}

bool VvtTriggerConfiguration::isVerboseTriggerSynchDetails() const {
	return engineConfiguration->verboseVVTDecoding;
}
