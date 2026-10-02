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

#if EFI_ENGINE_CONTROL
// Keep trim caches/read context out of the core frame while the deeper airmass
// calculation runs. This boundary must survive LTO; outputs use caller storage.
static __attribute__((noinline)) void prepareCylinderTrims(
		float rpm,
		float cycleFuelMass,
		float untrimmedAdvance,
		size_t calculationCylinderCount,
		const float* bankCorrections,
		float* cylinderFuelMasses,
		float* cylinderTiming,
		bool& fuelPublicationValid) {
	const AirmassConsumerLoadContext trimLoads(
			engineConfiguration->isInjectionEnabled ? AirmassConsumer::FuelTrim : AirmassConsumer::Count,
			engineConfiguration->isIgnitionEnabled ? AirmassConsumer::IgnitionTrim : AirmassConsumer::Count);
	Table3DInterpolationCache fuelTrimInterpolation(
			config->fuelTrimLoadBins, config->fuelTrimRpmBins, rpm, engineConfiguration->isInjectionEnabled);
	Table3DInterpolationCache ignitionTrimInterpolation(
			config->ignTrimLoadBins, config->ignTrimRpmBins, rpm, engineConfiguration->isIgnitionEnabled);
	for (size_t i = 0; i < calculationCylinderCount && i < MAX_CYLINDER_COUNT; i++) {
		float fuelTrim = 1;
		float ignitionTrim = 0;
		if (engineConfiguration->isInjectionEnabled) {
			const float load = trimLoads.get(AirmassConsumer::FuelTrim, i);
			fuelPublicationValid &= std::isfinite(load);
			if (std::isfinite(load)) {
				auto interpolation = fuelTrimInterpolation.prepare(load);
				fuelTrim = getCylinderFuelTrim(i, interpolation);
			}
		}
		if (engineConfiguration->isIgnitionEnabled) {
			const float load = trimLoads.get(AirmassConsumer::IgnitionTrim, i);
			// An unavailable ignition trim uses its neutral value locally.
			if (std::isfinite(load)) {
				auto interpolation = ignitionTrimInterpolation.prepare(load);
				ignitionTrim = getCylinderIgnitionTrim(i, interpolation);
			}
		}
		cylinderTiming[i] = untrimmedAdvance + ignitionTrim;
		const uint8_t bankIndex = engineConfiguration->cylinderBankSelect[i];
		if (bankIndex >= STFT_BANK_COUNT) {
			fuelPublicationValid = false;
			continue;
		}
		// Use this calculation's bank result consistently with its cylinder trims.
		const float mass = cycleFuelMass * bankCorrections[bankIndex] * fuelTrim;
		fuelPublicationValid &= std::isfinite(mass) && mass >= 0;
		cylinderFuelMasses[i] = mass;
	}
}
#endif

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
	publishLegacyAirmassConsumerLoads();
	auto clResult = fuelClosedLoopCorrection();

	float nextStage2Fraction;
	float nextInjectionDuration;
	float nextInjectionDurationStage2;
	{
		float injectionFuelMass = cycleFuelMass * getInjectionModeDurationMultiplier(getCurrentInjectionMode());

		nextStage2Fraction = engineConfiguration->isInjectionEnabled && engineConfiguration->enableStagedInjection
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
	// A disabled monitor still resets its timer and can restore a latched cut.
	const float lambdaMonitorLoad = engineConfiguration->lambdaProtectionEnable || engine->lambdaMonitor.isCut()
										  ? getAirmassConsumerLoad(AirmassConsumer::LambdaMonitor)
										  : 0;
	engine->lambdaMonitor.update(rpm, lambdaMonitorLoad);

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

	const size_t calculationCylinderCount = engine->engineState.cylinderCount;
	bool fuelPublicationValid = airmassCalculationValid && std::isfinite(cycleFuelMass) && cycleFuelMass >= 0 &&
								std::isfinite(nextStage2Fraction) && nextStage2Fraction >= 0 &&
								nextStage2Fraction <= 1 && std::isfinite(nextInjectionDuration) &&
								nextInjectionDuration >= 0 && std::isfinite(nextInjectionDurationStage2) &&
								nextInjectionDurationStage2 >= 0 && std::isfinite(nextInjectionOffset) &&
								calculationCylinderCount > 0 && calculationCylinderCount <= MAX_CYLINDER_COUNT;
	// These scratch arrays hold final values, avoiding a second pair of V12 arrays.
	float cylinderFuelMasses[MAX_CYLINDER_COUNT]{};
	float cylinderTiming[MAX_CYLINDER_COUNT]{};
	prepareCylinderTrims(
			rpm,
			cycleFuelMass,
			untrimmedAdvance,
			calculationCylinderCount,
			clResult.banks,
			cylinderFuelMasses,
			cylinderTiming,
			fuelPublicationValid);
	// Evaluate duty against the durations about to be published, outside the lock.
	const float totalInjectionTime = nextInjectionDuration * getNumberOfInjections(getCurrentInjectionMode());
	const float cycleDuration = getEngineCycleDuration(rpm);
	const bool nextShouldUpdateInjectionTiming = 100 * totalInjectionTime / cycleDuration < 90;
	const bool clearedShouldUpdateInjectionTiming = 0.0f / cycleDuration < 90;
#if EFI_UNIT_TEST
	if (engine->onCylinderFuelPrepared) {
		engine->onCylinderFuelPrepared();
	}
#endif
	{
		// A stop, fault or tune write must reject the entire prepared publication.
		chibios_rt::CriticalSectionLocker csl;
		auto admission = engine->airmassInjectionState.locked(csl);
		if (admission.isCalculationCurrent(fuelCalculation)) {
			fuelPublicationValid &= calculationCylinderCount == engine->engineState.cylinderCount;
			if (fuelPublicationValid) {
				injectionStage2Fraction = nextStage2Fraction;
				injectionDuration = nextInjectionDuration;
				injectionDurationStage2 = nextInjectionDurationStage2;
				injectionOffset = nextInjectionOffset;
				for (size_t i = 0; i < calculationCylinderCount; i++) {
					engine->cylinders[i].setInjectionMass(cylinderFuelMasses[i]);
				}
				shouldUpdateInjectionTiming = nextShouldUpdateInjectionTiming;
			} else {
				// Reject every fuel value together, including external legacy models.
				injectionStage2Fraction = 0;
				injectionDuration = 0;
				injectionDurationStage2 = 0;
				injectionOffset = 0;
				for (auto& cylinder : engine->cylinders) {
					cylinder.setInjectionMass(0);
				}
				shouldUpdateInjectionTiming = clearedShouldUpdateInjectionTiming;
			}
			for (size_t i = 0; i < calculationCylinderCount && i < MAX_CYLINDER_COUNT; i++) {
				engine->cylinders[i].setIgnitionTimingBtdc(cylinderTiming[i]);
			}
			admission.completeCalculation(fuelCalculation, fuelPublicationValid);
		}
	}

	const float trailingLoad = engineConfiguration->isIgnitionEnabled && engineConfiguration->enableTrailingSparks
									 ? getAirmassConsumerLoad(AirmassConsumer::TrailingSpark)
									 : NAN;
	trailingSparkAngle = std::isfinite(trailingLoad) ? interpolate3d(
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
