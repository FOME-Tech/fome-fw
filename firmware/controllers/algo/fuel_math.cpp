/**
 * @file	fuel_math.cpp
 * @brief	Fuel amount calculation logic
 *
 *
 * @date May 27, 2013
 * @author Andrey Belomutskiy, (c) 2012-2020
 *
 * This file is part of rusEfi - see http://rusefi.com
 *
 * rusEfi is free software; you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * rusEfi is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 * even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "pch.h"
#include "airmass_loads.h"

#include "airmass.h"
#include "alphan_airmass.h"
#include "blended_airmass.h"
#include "maf_airmass.h"
#include "speed_density_airmass.h"
#include "fuel_math.h"
#include "fuel_computer.h"
#include "injector_model.h"
#include "speed_density.h"
#include "speed_density_base.h"
#include "lua_hooks.h"

static mapEstimate_Map3D_t mapEstimationTable;

#if EFI_ENGINE_CONTROL

float getCrankingFuel3(float baseFuel, uint32_t revolutionCounterSinceStart) {

	float baseCrankingFuel;
	if (engineConfiguration->useRunningMathForCranking) {
		baseCrankingFuel = baseFuel;
	} else {
		// parameter is in milligrams, convert to grams
		baseCrankingFuel = engineConfiguration->cranking.baseFuel * 0.001f;
	}

	// Cranking fuel changes over time
	engine->engineState.crankingFuel.durationCoefficient =
			interpolate2d(revolutionCounterSinceStart, config->crankingCycleBins, config->crankingCycleCoef);

	/**
	 * Cranking fuel is different depending on engine coolant temperature
	 * If the sensor is failed, use 20 deg C
	 */
	auto clt = Sensor::get(SensorType::Clt).value_or(20);
	auto e0Mult = interpolate2d(clt, config->crankingFuelBins, config->crankingFuelCoef);

	bool alreadyWarned = false;
	if (e0Mult <= 0.1f) {
		warning(ObdCode::CUSTOM_ERR_ZERO_E0_MULT, "zero e0 multiplier");
		alreadyWarned = true;
	}

	if (engineConfiguration->flexCranking && Sensor::hasSensor(SensorType::FuelEthanolPercent)) {
		auto e85Mult = interpolate2d(clt, config->crankingFuelBins, config->crankingFuelCoefE100);

		if (e85Mult <= 0.1f) {
			warning(ObdCode::CUSTOM_ERR_ZERO_E85_MULT, "zero e85 multiplier");
			alreadyWarned = true;
		}

		auto flex = getFlexEthanolPercent();

		engine->engineState.crankingFuel.coolantTemperatureCoefficient =
				interpolateClamped(0, e0Mult, 85, e85Mult, flex);
	} else {
		engine->engineState.crankingFuel.coolantTemperatureCoefficient = e0Mult;
	}

	auto tps = Sensor::get(SensorType::DriverThrottleIntent);
	engine->engineState.crankingFuel.tpsCoefficient =
			tps.Valid ? interpolate2d(tps.Value, config->crankingTpsBins, config->crankingTpsCoef)
					  : 1; // in case of failed TPS, don't correct.

	floatms_t crankingFuel = baseCrankingFuel * engine->engineState.crankingFuel.durationCoefficient *
							 engine->engineState.crankingFuel.coolantTemperatureCoefficient *
							 engine->engineState.crankingFuel.tpsCoefficient;

	engine->engineState.crankingFuel.fuel = crankingFuel * 1000;

	// don't re-warn for zero fuel when we already warned for a more specific problem
	if (!alreadyWarned && crankingFuel <= 0) {
		warning(ObdCode::CUSTOM_ERR_ZERO_CRANKING_FUEL, "Cranking fuel value %f", crankingFuel);
	}
	return crankingFuel;
}

float getRunningFuel(float baseFuel) {
	ScopePerf perf(PE::GetRunningFuel);

	float iatCorrection = engine->fuelComputer.running.intakeTemperatureCoefficient;
	float cltCorrection = engine->fuelComputer.running.coolantTemperatureCoefficient;
	float postCrankingFuelCorrection = engine->fuelComputer.running.postCrankingFuelCorrection;
	float baroCorrection = engine->engineState.baroCorrection;

	efiAssert(ObdCode::CUSTOM_ERR_ASSERT, !std::isnan(iatCorrection), "NaN iatCorrection", 0);
	efiAssert(ObdCode::CUSTOM_ERR_ASSERT, !std::isnan(cltCorrection), "NaN cltCorrection", 0);
	efiAssert(ObdCode::CUSTOM_ERR_ASSERT, !std::isnan(postCrankingFuelCorrection), "NaN postCrankingFuelCorrection", 0);

	float correction = baroCorrection * iatCorrection * cltCorrection * postCrankingFuelCorrection;

#if EFI_ANTILAG_SYSTEM
	correction *= (1 + engine->antilagController.fuelALSCorrection / 100);
#endif /* EFI_ANTILAG_SYSTEM */

#if EFI_LAUNCH_CONTROL
	correction *= engine->launchController.getFuelCoefficient();
#endif

	float runningFuel = baseFuel * correction;

	efiAssert(ObdCode::CUSTOM_ERR_ASSERT, !std::isnan(runningFuel), "NaN runningFuel", 0);

	// Publish output state
	engine->fuelComputer.running.baseFuel = baseFuel * 1000;
	engine->fuelComputer.totalFuelCorrection = correction;
	engine->fuelComputer.running.fuel = runningFuel * 1000;

	return runningFuel;
}

static SpeedDensityAirmass sdAirmass(nullptr, mapEstimationTable);
static MafAirmass mafAirmass;
static AlphaNAirmass alphaNAirmass;
static BlendedAirmass blendedAirmass(sdAirmass, alphaNAirmass);

AirmassModelBase* getAirmassModel(engine_load_mode_e mode) {
	switch (mode) {
		case LM_SPEED_DENSITY:
			return &sdAirmass;
		case LM_REAL_MAF:
			return &mafAirmass;
		case LM_ALPHA_N:
			return &alphaNAirmass;
		case LM_SD_ALPHA_N:
			return &blendedAirmass;
#if EFI_LUA
		case LM_LUA:
			return &(getLuaAirmassModel());
#endif
#if EFI_UNIT_TEST
		case LM_MOCK:
			return engine->mockAirmassModel;
#endif
		default:
			firmwareError(ObdCode::CUSTOM_ERR_ASSERT, "Invalid airmass mode %d", engineConfiguration->fuelAlgorithm);
			return nullptr;
	}
}

static void clearUnusedLambdaDiagnostics() {
	engine->fuelComputer.resetResolvedLambdaLoad();
	engine->fuelComputer.afrTableYAxis = 0;
	engine->fuelComputer.targetLambda = 0;
	engine->fuelComputer.targetAFR = 0;
	engine->fuelComputer.stoichiometricRatio = 0;
	engine->fuelComputer.running.baseFuel = 0;
	engine->fuelComputer.running.fuel = 0;
}

static float getBlendedBaseFuelMass(float rpm) {
	engine->outputChannels.blendedLambdaLoad = 0;
	engine->fuelComputer.resetResolvedLambdaLoad();
	const auto calculationToken = engine->airmassInjectionState.publicationEpoch();
	const auto evaluation = blendedAirmass.getAirmassForFuel(rpm);
	if (!engine->airmassInjectionState.isCalculationCurrent(calculationToken)) {
		return 0;
	}
	if (!evaluation.Airmass.Valid) {
		engine->fuelComputer.sdAirMassInOneCylinder = 0;
		engine->fuelComputer.normalizedCylinderFilling = 0;
		engine->fuelComputer.afrTableYAxis = 0;
		engine->fuelComputer.targetLambda = 0;
		engine->fuelComputer.targetAFR = 0;
		engine->engineState.fuelingLoad = 0;
		engine->engineState.ignitionLoad = 0;
		engine->engineState.airflowEstimate = 0;
		engine->engineState.baseFuel = 0;
		engine->engineState.airmassLoads.Valid = false;
		engine->airmassInjectionState.rejectCalculation(evaluation.Fault);
		return 0;
	}
	const auto& airmass = evaluation.Airmass.Result;
	engine->fuelComputer.sdAirMassInOneCylinder = airmass.CylinderAirmass;
	engine->fuelComputer.normalizedCylinderFilling = evaluation.NormalizedFilling;
	engine->engineState.fuelingLoad = getAirmassSelectedLoad(AFR_None, airmass.EngineLoadPercent);
	const float lambdaLoad = engine->fuelComputer.getTargetLambdaLoadAxis(airmass.EngineLoadPercent);
	engine->engineState.ignitionLoad = engineConfiguration->isIgnitionEnabled
											 ? engine->fuelComputer.getLoadOverride(
													   airmass.EngineLoadPercent, engineConfiguration->ignOverrideMode)
											 : 0;
	const float gramsPerCycle = airmass.CylinderAirmass * engine->engineState.cylinderCount;
	engine->engineState.airflowEstimate = gramsPerCycle / getEngineCycleDuration(rpm) * 3600000 / 1000;
	float baseFuelMass = 0;
	const bool lambdaRequired = isAirmassLambdaTargetRequired();
	if (lambdaRequired) {
		baseFuelMass = engine->fuelComputer.getCycleFuelWithResolvedLoad(airmass.CylinderAirmass, rpm, lambdaLoad);
		if (!std::isfinite(baseFuelMass) || baseFuelMass < 0) {
			engine->engineState.baseFuel = 0;
			engine->fuelComputer.resetResolvedLambdaLoad();
			engine->airmassInjectionState.rejectCalculation(AirmassInjectionFault::Result);
			return 0;
		}
	} else {
		clearUnusedLambdaDiagnostics();
	}
	if (engineConfiguration->isInjectionEnabled) {
		const float correction = engineConfiguration->globalFuelCorrection;
		if (!std::isfinite(correction) || correction < 0 || !std::isfinite(baseFuelMass * correction)) {
			engine->engineState.baseFuel = 0;
			engine->fuelComputer.resetResolvedLambdaLoad();
			engine->airmassInjectionState.rejectCalculation(AirmassInjectionFault::Result);
			return 0;
		}
		baseFuelMass *= correction;
	} else {
		baseFuelMass = 0;
		engine->fuelComputer.running.baseFuel = 0;
		engine->fuelComputer.running.fuel = 0;
	}
	engine->outputChannels.blendedLambdaLoad = lambdaRequired ? lambdaLoad : 0;
	engine->engineState.baseFuel = baseFuelMass;
	// engine2 still owns the final readiness transition after all per-cylinder
	// fuel quantities and durations have been checked and published.
	engine->engineState.airmassCalculationValid = true;
	engine->airmassInjectionState.acceptCalculation(
			evaluation.Degraded		   ? evaluation.Fault
			: hasAirmassLoadFallback() ? AirmassInjectionFault::Load
									   : AirmassInjectionFault::None);
	return baseFuelMass;
}

// Per-cylinder base fuel mass. Keep the larger composite result in a separate
// call frame so legacy modes retain their compact standalone calculation path.
static float getBaseFuelMass(float rpm) {
	ScopePerf perf(PE::GetBaseFuel);
	engine->engineState.airmassCalculationValid = false;
	updateBlendedVeAnalyzeQualification(rpm);
	if (engineConfiguration->fuelAlgorithm == LM_SD_ALPHA_N) {
		return getBlendedBaseFuelMass(rpm);
	}
	const bool revisedStandalone = engineConfiguration->fuelAlgorithm == LM_SPEED_DENSITY ||
								   engineConfiguration->fuelAlgorithm == LM_ALPHA_N ||
								   engineConfiguration->fuelAlgorithm == LM_REAL_MAF;
	const auto calculationToken = engine->airmassInjectionState.publicationEpoch();
	const auto rejectStandalone = [revisedStandalone, calculationToken](AirmassInjectionFault fault) {
		chibios_rt::CriticalSectionLocker csl;
		if (revisedStandalone && engine->airmassInjectionState.isCalculationCurrent(calculationToken)) {
			// Retaining completed fuel during evaluation must not retain it after
			// an input/result has actually failed. A stale evaluation cannot close
			// admission for a newer completed publication.
			engine->airmassInjectionState.rejectCalculation(fault);
		}
	};
	if (!isAirmassConfigurationValid()) {
		rejectStandalone(AirmassInjectionFault::Configuration);
		return 0;
	}

	// airmass modes - get airmass first, then convert to fuel
	auto model = getAirmassModel(engineConfiguration->fuelAlgorithm);
	efiAssert(ObdCode::CUSTOM_ERR_ASSERT, model != nullptr, "Invalid airmass mode", 0.0f);

	AirmassResult airmass;
	if (revisedStandalone) {
		const auto evaluation = engineConfiguration->fuelAlgorithm == LM_SPEED_DENSITY
									  ? sdAirmass.getAirmassForFuel(rpm)
							  : engineConfiguration->fuelAlgorithm == LM_ALPHA_N ? alphaNAirmass.getAirmassForFuel(rpm)
																				 : mafAirmass.getAirmassForFuel(rpm);
		if (!engine->airmassInjectionState.isCalculationCurrent(calculationToken)) {
			return 0;
		}
		if (!evaluation.Valid) {
			rejectStandalone(AirmassInjectionFault::Result);
			engine->engineState.airmassLoads.Valid = false;
			engine->engineState.fuelingLoad = 0;
			engine->engineState.ignitionLoad = 0;
			engine->fuelComputer.sdAirMassInOneCylinder = 0;
			engine->fuelComputer.normalizedCylinderFilling = 0;
			engine->engineState.baseFuel = 0;
			return 0;
		}
		airmass = evaluation.Result;
		engine->airmassInjectionState.acceptCalculation(
				evaluation.Degraded		   ? AirmassInjectionFault::Sensor
				: hasAirmassLoadFallback() ? AirmassInjectionFault::Load
										   : AirmassInjectionFault::None);
	} else {
		airmass = model->getAirmass(rpm, true);
	}

	// Plop some state for others to read
	float normalizedCylinderFilling = 100 * airmass.CylinderAirmass / getStandardAirCharge();
	engine->fuelComputer.sdAirMassInOneCylinder = airmass.CylinderAirmass;
	engine->fuelComputer.normalizedCylinderFilling = normalizedCylinderFilling;
	engine->engineState.fuelingLoad = getAirmassSelectedLoad(AFR_None, airmass.EngineLoadPercent);
	const float ignitionLoad = engineConfiguration->isIgnitionEnabled
									 ? engine->fuelComputer.getLoadOverride(
											   airmass.EngineLoadPercent, engineConfiguration->ignOverrideMode)
									 : 0;
	engine->engineState.ignitionLoad = std::isfinite(ignitionLoad) ? ignitionLoad : 0;

	auto gramPerCycle = airmass.CylinderAirmass * engine->engineState.cylinderCount;
	auto gramPerMs = rpm == 0 ? 0 : gramPerCycle / getEngineCycleDuration(rpm);

	// convert g/s -> kg/h
	engine->engineState.airflowEstimate = gramPerMs * 3600000 /* milliseconds per hour */ / 1000 /* grams per kg */;
	;

	float baseFuelMass = 0;
	if (revisedStandalone) {
		if (isAirmassLambdaTargetRequired()) {
			const float lambdaLoad = engine->fuelComputer.getTargetLambdaLoadAxis(airmass.EngineLoadPercent);
			baseFuelMass = engine->fuelComputer.getCycleFuelWithResolvedLoad(airmass.CylinderAirmass, rpm, lambdaLoad);
		} else {
			clearUnusedLambdaDiagnostics();
		}
	} else {
		baseFuelMass = engine->fuelComputer.getCycleFuel(airmass.CylinderAirmass, rpm, airmass.EngineLoadPercent);
	}
	if (!std::isfinite(baseFuelMass) || baseFuelMass < 0) {
		rejectStandalone(AirmassInjectionFault::Result);
		engine->engineState.baseFuel = 0;
		return 0;
	}
	if (engineConfiguration->isInjectionEnabled) {
		baseFuelMass *= engineConfiguration->globalFuelCorrection;
	} else {
		baseFuelMass = 0;
		engine->fuelComputer.running.baseFuel = 0;
		engine->fuelComputer.running.fuel = 0;
	}
	if (!std::isfinite(baseFuelMass) || baseFuelMass < 0) {
		rejectStandalone(AirmassInjectionFault::Result);
		engine->engineState.baseFuel = 0;
		return 0;
	}
	engine->engineState.baseFuel = baseFuelMass;
	engine->engineState.airmassCalculationValid = true;
	return baseFuelMass;
}

angle_t getInjectionOffset(float rpm, float load) {
	if (std::isnan(rpm)) {
		return 0; // error already reported
	}

	if (std::isnan(load)) {
		return 0; // error already reported
	}

	angle_t value = interpolate3d(config->injectionPhase, config->injPhaseLoadBins, load, config->injPhaseRpmBins, rpm);

	if (std::isnan(value)) {
		// we could be here while resetting configuration for example
		// huh? what? when do we have RPM while resetting configuration? is that CI edge case? shall we fix CI?
		warning(ObdCode::CUSTOM_ERR_6569, "phase map not ready");
		return 0;
	}

	angle_t result = value;
	wrapAngle(result, "inj offset#2", ObdCode::CUSTOM_ERR_6553);
	return result;
}

/**
 * Number of injections using each injector per engine cycle
 * @see getNumberOfSparks
 */
int getNumberOfInjections(injection_mode_e mode) {
	switch (mode) {
		case IM_SIMULTANEOUS:
		case IM_SINGLE_POINT:
			return engine->engineState.cylinderCount;
		case IM_BATCH:
			return 2;
		case IM_SEQUENTIAL:
			return 1;
		default:
			firmwareError(ObdCode::CUSTOM_ERR_INVALID_INJECTION_MODE, "Unexpected injection_mode_e %d", mode);
			return 1;
	}
}

float getInjectionModeDurationMultiplier(injection_mode_e mode) {
	switch (mode) {
		case IM_SIMULTANEOUS: {
			auto cylCount = engine->engineState.cylinderCount;

			if (cylCount == 0) {
				// we can end up here during configuration reset
				return 0;
			}

			return 1.0f / cylCount;
		}
		case IM_SEQUENTIAL:
		case IM_SINGLE_POINT:
			return 1;
		case IM_BATCH:
			return 0.5f;
		default:
			firmwareError(ObdCode::CUSTOM_ERR_INVALID_INJECTION_MODE, "Unexpected injection_mode_e %d", mode);
			return 0;
	}
}

percent_t getInjectorDutyCycle(float rpm) {
	auto mode = getCurrentInjectionMode();
	floatms_t totalInjectiorAmountPerCycle = engine->engineState.injectionDuration * getNumberOfInjections(mode);
	floatms_t engineCycleDuration = getEngineCycleDuration(rpm);
	return 100 * totalInjectiorAmountPerCycle / engineCycleDuration;
}

percent_t getInjectorDutyCycleStage2(float rpm) {
	auto mode = getCurrentInjectionMode();
	floatms_t totalInjectiorAmountPerCycle = engine->engineState.injectionDurationStage2 * getNumberOfInjections(mode);
	floatms_t engineCycleDuration = getEngineCycleDuration(rpm);
	return 100 * totalInjectiorAmountPerCycle / engineCycleDuration;
}

static float getCycleFuelMass(bool isCranking, float baseFuelMass) {
	if (isCranking) {
		return getCrankingFuel(baseFuelMass);
	} else {
		return getRunningFuel(baseFuelMass);
	}
}

/**
 * @returns	Mass of each individual fuel injection, in grams
 *     in case of single point injection mode the amount of fuel into all cylinders, otherwise the amount for one
 * cylinder
 */
float getCycleInjectionMass(float rpm, bool isCranking) {
	ScopePerf perf(PE::GetInjectionDuration);

#if EFI_SHAFT_POSITION_INPUT
	// Always update base fuel - some cranking modes use it
	float baseFuelMass = getBaseFuelMass(rpm);

	floatms_t tpsAccelEnrich = engine->module<TpsAccelEnrichment>()->getTpsEnrichment();
	efiAssert(ObdCode::CUSTOM_ERR_ASSERT, !std::isnan(tpsAccelEnrich), "NaN tpsAccelEnrich", 0);
	engine->engineState.tpsAccelEnrich = tpsAccelEnrich;

	// A failed airmass input must not be revived by fixed cranking fuel or AE.
	if (!engine->engineState.airmassCalculationValid || !engineConfiguration->isInjectionEnabled) {
		return 0;
	}
	float cycleFuelMass = getCycleFuelMass(isCranking, baseFuelMass);
	efiAssert(ObdCode::CUSTOM_ERR_ASSERT, !std::isnan(cycleFuelMass), "NaN cycleFuelMass", 0);

	if (engine->module<DfcoController>()->cutFuel()) {
		// If decel fuel cut, zero out fuel
		cycleFuelMass = 0;
	}

	// Prepare injector flow rate & deadtime
	engine->module<InjectorModelPrimary>()->prepare();

	if (engineConfiguration->enableStagedInjection) {
		engine->module<InjectorModelSecondary>()->prepare();
	}

	// For legacy reasons, the TPS accel table is in units of milliseconds, so we have to convert BACK to mass
	float tpsFuelMass = engine->module<InjectorModelPrimary>()->getFuelMassForDuration(tpsAccelEnrich);

	return cycleFuelMass + tpsFuelMass;
#else
	return 0;
#endif
}

/**
 * @brief	Initialize fuel map data structure
 * @note this method has nothing to do with fuel map VALUES - it's job
 * is to prepare the fuel map data structure for 3d interpolation
 */
void initFuelMap() {
	mapEstimationTable.init(config->mapEstimateTable, config->mapEstimateTpsBins, config->mapEstimateRpmBins);
}

/**
 * @brief Engine warm-up fuel correction.
 */
float getCltFuelCorrection() {
	const auto clt = Sensor::get(SensorType::Clt);

	if (!clt) {
		return 1; // this error should be already reported somewhere else, let's just handle it
	}

	return interpolate2d(clt.Value, config->cltFuelCorrBins, config->cltFuelCorr);
}

float getIatFuelCorrection() {
	const auto iat = Sensor::get(SensorType::Iat);

	if (!iat) {
		return 1; // this error should be already reported somewhere else, let's just handle it
	}

	return interpolate2d(iat.Value, config->iatFuelCorrBins, config->iatFuelCorr);
}

float getBaroCorrection() {
	if (Sensor::hasSensor(SensorType::BarometricPressure)) {
		// Default to 1atm if failed
		float pressure = Sensor::get(SensorType::BarometricPressure).value_or(101.325f);

		float correction = interpolate3d(
				config->baroCorrTable,
				config->baroCorrPressureBins,
				pressure,
				config->baroCorrRpmBins,
				Sensor::getOrZero(SensorType::Rpm));

		if (std::isnan(correction) || correction < 0.01) {
			warning(ObdCode::OBD_Barometric_Press_Circ_Range_Perf, "Invalid baro correction %f", correction);
			return 1;
		}

		return correction;
	} else {
		return 1;
	}
}

percent_t getFuelALSCorrection(float rpm) {
#if EFI_ANTILAG_SYSTEM
	if (engine->antilagController.isAntilagCondition) {
		float throttleIntent = Sensor::getOrZero(SensorType::DriverThrottleIntent);
		auto AlsFuelAdd = interpolate3d(
				config->ALSFuelAdjustment,
				config->alsFuelAdjustmentLoadBins,
				throttleIntent,
				config->alsFuelAdjustmentrpmBins,
				rpm);
		return AlsFuelAdd;
	} else
#endif /* EFI_ANTILAG_SYSTEM */
	{
		return 0;
	}
}

#if EFI_ENGINE_CONTROL
/**
 * @return Duration of fuel injection while craning
 */
float getCrankingFuel(float baseFuel) {
	return getCrankingFuel3(baseFuel, engine->rpmCalculator.getRevolutionCounterSinceStart());
}

/**
 * Standard cylinder air charge - 100% VE at standard temperature, grams per cylinder
 *
 * Should we bother caching 'getStandardAirCharge' result or can we afford to run the math every time we calculate fuel?
 */
float getStandardAirCharge() {
	float totalDisplacement = engineConfiguration->displacement;
	float cylDisplacement = totalDisplacement / engine->engineState.cylinderCount;

	// Calculation of 100% VE air mass in g/cyl - 1 cylinder filling at 1.204/L
	// 101.325kpa, 20C
	return idealGasLaw(cylDisplacement, 101.325f, 273.15f + 20.0f);
}

float getCylinderFuelTrim(
		size_t cylinderNumber, const PreparedTable3DInterpolation<TRIM_SIZE, TRIM_SIZE>& interpolation) {
	auto trimPercent = interpolation.getValue(config->fuelTrims[cylinderNumber].table);

	// Convert from percent +- to multiplier
	// 5% -> 1.05
	return (100 + trimPercent) / 100;
}

static Hysteresis stage2Hysteresis;

float getStage2InjectionFraction(float rpm, float load) {
	if (!engineConfiguration->enableStagedInjection || !std::isfinite(load) || !std::isfinite(rpm)) {
		return 0;
	}

	float frac = 0.01f * interpolate3d(
								 config->injectorStagingTable,
								 config->injectorStagingLoadBins,
								 load,
								 config->injectorStagingRpmBins,
								 rpm);

	// don't allow very small fraction, with some hysteresis
	if (!stage2Hysteresis.test(frac, 0.1, 0.03)) {
		return 0;
	}

	// Clamp to 90%
	if (frac > 0.9) {
		frac = 0.9;
	}

	return frac;
}

#endif
#endif
