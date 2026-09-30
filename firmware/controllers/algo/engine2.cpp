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
#include "airmass_loads.h"
#include "closed_loop_fuel.h"
#include "launch_control.h"
#include "injector_model.h"
#include "tunerstudio.h"
#include "gitversion.h"

#if !EFI_UNIT_TEST
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
	auto fuelCalculation = engine->airmassInjectionState.beginCalculation(
			engineConfiguration->fuelAlgorithm, rpm, engine->getGlobalConfigurationVersion());

	if (isCranking) {
		crankingTimer.reset(nowNt);
	}

	engine->fuelComputer.running.timeSinceCrankingInSecs = crankingTimer.getElapsedSeconds(nowNt);

	if (engineConfiguration->isIgnitionEnabled) {
		engine->ignitionState.updateDwell(rpm, isCranking);
	} else {
		engine->ignitionState.sparkDwell = 0;
		engine->ignitionState.dwellAngle = 0;
	}

	// todo: move this into slow callback, no reason for IAT corr to be here
	engine->fuelComputer.running.intakeTemperatureCoefficient = getIatFuelCorrection();
	// todo: move this into slow callback, no reason for CLT corr to be here
	engine->fuelComputer.running.coolantTemperatureCoefficient = getCltFuelCorrection();

	engine->module<DfcoController>()->update();

	// post-cranking fuel enrichment.
	if (engineConfiguration->postCrankingFuelUseTable) {
		float postCrankingCorr = interpolate3d(
				config->postCrankingEnrichTable,
				config->postCrankingEnrichTempBins,
				Sensor::getOrZero(SensorType::Clt),
				config->postCrankingEnrichRuntimeBins,
				engine->fuelComputer.running.timeSinceCrankingInSecs);

		engine->fuelComputer.running.postCrankingFuelCorrection = clampF(1, postCrankingCorr, 5);
	} else {
		// for compatibility reasons, apply only if the factor is greater than unity (only allow adding fuel)
		if (engineConfiguration->postCrankingFactor > 1.0f) {
			// use interpolation for correction taper
			engine->fuelComputer.running.postCrankingFuelCorrection = interpolateClamped(
					0.0f,
					engineConfiguration->postCrankingFactor,
					engineConfiguration->postCrankingDurationSec,
					1.0f,
					engine->fuelComputer.running.timeSinceCrankingInSecs);
		} else {
			engine->fuelComputer.running.postCrankingFuelCorrection = 1.0f;
		}
	}

	baroCorrection = getBaroCorrection();

	auto tps = Sensor::get(SensorType::Tps1);
	updateTChargeK(rpm, tps.value_or(0));

	float cycleFuelMass =
			getCycleInjectionMass(rpm, isCranking) * engine->engineState.lua.fuelMult + engine->engineState.lua.fuelAdd;
	if (!airmassCalculationValid || !engineConfiguration->isInjectionEnabled) {
		cycleFuelMass = 0;
	}
	auto clResult = fuelClosedLoopCorrection();

	float nextStage2Fraction;
	float nextInjectionDuration;
	float nextInjectionDurationStage2;
	{
		float injectionFuelMass = cycleFuelMass * getInjectionModeDurationMultiplier(getCurrentInjectionMode());

		nextStage2Fraction = engineConfiguration->isInjectionEnabled
								   ? getStage2InjectionFraction(rpm, getAirmassConsumerLoad(AirmassConsumer::Staging))
								   : 0;
		float stage2InjectionMass = injectionFuelMass * nextStage2Fraction;
		float stage1InjectionMass = injectionFuelMass - stage2InjectionMass;

		// Store the pre-wall wetting injection duration for scheduling purposes only, not the actual injection duration
		nextInjectionDuration =
				airmassCalculationValid && engineConfiguration->isInjectionEnabled
						? engine->module<InjectorModelPrimary>()->getInjectionDuration(stage1InjectionMass)
						: 0;
		nextInjectionDurationStage2 =
				airmassCalculationValid && engineConfiguration->isInjectionEnabled &&
								engineConfiguration->enableStagedInjection
						? engine->module<InjectorModelSecondary>()->getInjectionDuration(stage2InjectionMass)
						: 0;
	}

	float nextInjectionOffset = engineConfiguration->isInjectionEnabled
									  ? getInjectionOffset(rpm, getAirmassConsumerLoad(AirmassConsumer::InjectionPhase))
									  : 0;
	engine->lambdaMonitor.update(rpm, getAirmassConsumerLoad(AirmassConsumer::LambdaMonitor));

	// Keep torque requests and cut state current even when spark outputs are disabled.
	const float torqueRetard = engine->torqueReductionController.update();
	float untrimmedAdvance = 0;
	if (engineConfiguration->isIgnitionEnabled) {
		engine->ignitionState.updateAdvanceCorrections(getAirmassConsumerLoad(AirmassConsumer::IgnitionIat));
		untrimmedAdvance =
				engine->ignitionState.getAdvance(rpm, ignitionLoad, isCranking) * engine->ignitionState.luaTimingMult +
				engine->ignitionState.luaTimingAdd - torqueRetard;
	} else {
		engine->ignitionState.cltTimingCorrection = 0;
		engine->ignitionState.timingIatCorrection = 0;
		engine->ignitionState.timingPidCorrection = 0;
		engine->ignitionState.dfcoTimingRetard = 0;
		for (size_t i = 0; i < efi::size(config->ignBlends); i++) {
			engine->outputChannels.ignBlendParameter[i] = 0;
			engine->outputChannels.ignBlendBias[i] = 0;
			engine->outputChannels.ignBlendOutput[i] = 0;
			engine->outputChannels.ignBlendYAxis[i] = 0;
		}
	}

	// that's weird logic. also seems broken for two stroke?
	engine->outputChannels.ignitionAdvance =
			(float)(untrimmedAdvance > FOUR_STROKE_CYCLE_DURATION / 2 ? untrimmedAdvance - FOUR_STROKE_CYCLE_DURATION
																	  : untrimmedAdvance);

	// compute per-bank fueling
	for (size_t i = 0; i < STFT_BANK_COUNT; i++) {
		engine->stftCorrection[i] = clResult.banks[i];
	}

	bool fuelPublicationValid = airmassCalculationValid && std::isfinite(cycleFuelMass) && cycleFuelMass >= 0 &&
								std::isfinite(nextStage2Fraction) && nextStage2Fraction >= 0 &&
								nextStage2Fraction <= 1 && std::isfinite(nextInjectionDuration) &&
								nextInjectionDuration >= 0 && std::isfinite(nextInjectionDurationStage2) &&
								nextInjectionDurationStage2 >= 0 && std::isfinite(nextInjectionOffset) &&
								engine->engineState.cylinderCount > 0 &&
								engine->engineState.cylinderCount <= MAX_CYLINDER_COUNT;
	float cylinderFuelTrims[MAX_CYLINDER_COUNT]{};
	float cylinderIgnitionTrims[MAX_CYLINDER_COUNT]{};
	for (size_t i = 0; i < engine->engineState.cylinderCount && i < MAX_CYLINDER_COUNT; i++) {
		cylinderFuelTrims[i] = 1;
		if (engineConfiguration->isInjectionEnabled) {
			const float load = getAirmassConsumerLoad(AirmassConsumer::FuelTrim, i);
			fuelPublicationValid &= std::isfinite(load);
			if (std::isfinite(load)) {
				PreparedTable3DInterpolation interpolation(
						config->fuelTrimLoadBins, load, config->fuelTrimRpmBins, rpm);
				cylinderFuelTrims[i] = getCylinderFuelTrim(i, interpolation);
			}
		}
		if (engineConfiguration->isIgnitionEnabled) {
			const float load = getAirmassConsumerLoad(AirmassConsumer::IgnitionTrim, i);
			// An unavailable ignition trim uses its neutral value locally.
			if (std::isfinite(load)) {
				PreparedTable3DInterpolation interpolation(config->ignTrimLoadBins, load, config->ignTrimRpmBins, rpm);
				cylinderIgnitionTrims[i] = getCylinderIgnitionTrim(i, interpolation);
			}
		}
	}

	{
		// Keep a stale/reentrant calculation from publishing after a stop, fault, or tune write.
		// Only the small final per-cylinder publication is locked; table/model evaluation stays outside.
		chibios_rt::CriticalSectionLocker csl;
		if (engine->airmassInjectionState.isCalculationCurrent(fuelCalculation)) {
			injectionStage2Fraction = nextStage2Fraction;
			injectionDuration = nextInjectionDuration;
			injectionDurationStage2 = nextInjectionDurationStage2;
			injectionOffset = nextInjectionOffset;
			for (size_t i = 0; i < engine->engineState.cylinderCount && i < MAX_CYLINDER_COUNT; i++) {
				uint8_t bankIndex = engineConfiguration->cylinderBankSelect[i];
				if (bankIndex >= STFT_BANK_COUNT) {
					fuelPublicationValid = false;
					continue;
				}
				auto bankTrim = engine->stftCorrection[bankIndex];

				// Apply both per-bank and per-cylinder trims.
				auto cylinderFuelMass = cycleFuelMass * bankTrim * cylinderFuelTrims[i];
				fuelPublicationValid &= std::isfinite(cylinderFuelMass) && cylinderFuelMass >= 0;
				engine->cylinders[i].setInjectionMass(cylinderFuelMass);
				engine->cylinders[i].setIgnitionTimingBtdc(untrimmedAdvance + cylinderIgnitionTrims[i]);
			}
		}
		shouldUpdateInjectionTiming = getInjectorDutyCycle(rpm) < 90;
		engine->airmassInjectionState.completeCalculation(fuelCalculation, fuelPublicationValid);
	}

	const float trailingLoad = getAirmassConsumerLoad(AirmassConsumer::TrailingSpark);
	trailingSparkAngle = engineConfiguration->isIgnitionEnabled && std::isfinite(trailingLoad)
							   ? interpolate3d(
										 config->trailingIgnitionTable,
										 config->trailingIgnitionLoadBins,
										 trailingLoad,
										 config->trailingIgnitionRpmBins,
										 rpm)
							   : 0;

	multispark.count = engineConfiguration->isIgnitionEnabled ? getMultiSparkCount(rpm) : 0;
#if EFI_TUNER_STUDIO
	engine->outputChannels.multiSparkCounter = multispark.count;
#endif

#if EFI_LAUNCH_CONTROL
	engine->launchController.update();
#endif // EFI_LAUNCH_CONTROL

#if EFI_ANTILAG_SYSTEM
	engine->antilagController.update();
#endif // EFI_ANTILAG_SYSTEM
#endif // EFI_ENGINE_CONTROL
}

void EngineState::updateTChargeK(float rpm, float tps) {
#if EFI_ENGINE_CONTROL
	float newTCharge = engine->fuelComputer.getTCharge(rpm, tps);
	if (!std::isnan(newTCharge)) {
		// control the rate of change or just fill with the initial value
		efitick_t nowNt = getTimeNowNt();
		float secsPassed = timeSinceLastTChargeK.getElapsedSeconds(nowNt);
		sd.tCharge = (sd.tChargeK == 0) ? newTCharge
										: limitRateOfChange(
												  newTCharge,
												  sd.tCharge,
												  engineConfiguration->tChargeAirIncrLimit,
												  engineConfiguration->tChargeAirDecrLimit,
												  secsPassed);
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

	return mode != VVT_INACTIVE && mode != VVT_TOYOTA_3_TOOTH && mode != VVT_HONDA_K_INTAKE && mode != VVT_MAP_V_TWIN &&
		   mode != VVT_SINGLE_TOOTH;
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
		case VVT_HONDA_K24Z_EXHAUST:
			return trigger_type_e::TT_HONDA_K24Z_CAM_3;
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
		case VVT_HONDA_J_6_2:
			return trigger_type_e::TT_HONDA_J_CAM_6_2;
		default:
			firmwareError("getVvtTriggerType for %s", getVvt_mode_e(vvtMode));
			return trigger_type_e::TT_ONE; // we have to return something for the sake of -Werror=return-type
	}
}

trigger_config_s VvtTriggerConfiguration::getType() const {
	if (!needsTriggerDecoder()) {
		return {trigger_type_e::TT_UNUSED, 0, 0};
	}

	// Convert from VVT type to trigger_config_s
	return {getVvtTriggerType(getVvtMode()), 0, 0};
}

bool VvtTriggerConfiguration::isVerboseTriggerSynchDetails() const {
	return engineConfiguration->verboseVVTDecoding;
}
