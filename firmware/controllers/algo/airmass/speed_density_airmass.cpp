#include "pch.h"
#include "speed_density_airmass.h"
#include "idle_thread.h"

AirmassResult SpeedDensityAirmass::getAirmass(float rpm, bool postState) {
	ScopePerf perf(PE::GetSpeedDensityFuel);

	return evaluateAirmass(rpm, DiagnosticsTarget(postState)).Result;
}

AirmassResult SpeedDensityAirmass::getAirmass(float rpm, float map, bool postState) {
	return evaluateAirmass(rpm, map, DiagnosticsTarget(postState)).Result;
}

AirmassEvaluation SpeedDensityAirmass::evaluateAirmass(float rpm, AirmassDiagnostics* diagnostics) const {
	return evaluateAirmass(rpm, DiagnosticsTarget(diagnostics));
}

AirmassEvaluation SpeedDensityAirmass::getAirmassForFuel(float rpm) const {
	return evaluateAirmass(rpm, DiagnosticsTarget(true));
}

AirmassEvaluation SpeedDensityAirmass::evaluateAirmass(float rpm, const DiagnosticsTarget& diagnostics) const {
	AirmassInputs inputs;
	captureInputs(rpm, inputs);
	return evaluateAirmass(inputs, diagnostics);
}

AirmassEvaluation SpeedDensityAirmass::evaluateAirmass(float rpm, float map, AirmassDiagnostics* diagnostics) const {
	return evaluateAirmass(rpm, map, DiagnosticsTarget(diagnostics));
}

AirmassEvaluation
SpeedDensityAirmass::evaluateAirmass(float rpm, float map, const DiagnosticsTarget& diagnostics) const {
	AirmassInputs inputs;
	captureAirmassInputs(rpm, inputs, nullptr, false);
	inputs.EffectiveMap = {map, 0, false, std::isfinite(map) && map >= 0 && map <= 1000, false};
	return evaluateAirmass(inputs, diagnostics);
}

AirmassEvaluation
SpeedDensityAirmass::evaluateAirmass(AirmassInputs& inputs, const DiagnosticsTarget& diagnostics) const {
	AirmassEvaluation evaluation;
	inputs.NativeLoad = inputs.EffectiveMap.Map;
	diagnostics.map(inputs.EffectiveMap);
	diagnostics.temperature(inputs);
	diagnostics.pressure(inputs, 1, false);
	if (!inputs.EffectiveMap.Valid || !inputs.TemperatureValid || !std::isfinite(inputs.Rpm) || inputs.Rpm <= 0 ||
		!std::isfinite(inputs.Displacement) || inputs.Displacement <= 0 || inputs.CylinderCount <= 0) {
		return evaluation;
	}
	auto ve = evaluateVe(inputs, inputs.NativeLoad, diagnostics);
	const float mass = getAirmassImpl(
			ve.Ve * PERCENT_DIV,
			inputs.EffectiveMap.Map,
			inputs.TemperatureK,
			inputs.Displacement,
			inputs.CylinderCount);
	evaluation.Result = {mass, inputs.NativeLoad};
	evaluation.Degraded = inputs.TemperatureFallback || inputs.EffectiveMap.Fallback || ve.Fallback;
	evaluation.Valid = ve.Valid && std::isfinite(mass) && mass >= 0;
	if (evaluation.Valid) {
		evaluation.Valid = diagnostics.consumers(inputs, mass);
	}
	if (!evaluation.Valid) {
		evaluation.Result.CylinderAirmass = 0;
	}
	return evaluation;
}

float SpeedDensityAirmass::getAirflow(float rpm, float map, bool postState) {
	auto airmassResult = getAirmass(rpm, map, postState);

	float massPerCycle = airmassResult.CylinderAirmass * engine->engineState.cylinderCount;

	if (!engineConfiguration->twoStroke) {
		// 4 stroke engines only do a half cycle per rev
		massPerCycle = massPerCycle / 2;
	}

	// g/s
	return massPerCycle * rpm / 60;
}

float SpeedDensityAirmass::getMap(float rpm, bool postState) const {
	auto evaluation = evaluateMap(rpm);
	DiagnosticsTarget(postState).map(evaluation);
	return evaluation.Map;
}

MapEvaluation SpeedDensityAirmass::evaluateMap(float rpm) const {
	AirmassInputs inputs;
	captureInputs(rpm, inputs);
	return inputs.EffectiveMap;
}

void captureAirmassInputs(float rpm, AirmassInputs& inputs, const ValueProvider3D* estimate, bool resolveMap) {
	{
		chibios_rt::CriticalSectionLocker csl;
		inputs.PublicationEpoch = engine->airmassInjectionState.publicationEpoch();
		inputs.ConfigurationVersion = engine->getGlobalConfigurationVersion();
		inputs.ActiveStrategy = engineConfiguration->fuelAlgorithm;
		inputs.HasPublicationContext = true;
	}
	inputs.Rpm = rpm;
	inputs.Tps = Sensor::get(SensorType::Tps1);
	inputs.MeasuredMap = Sensor::get(SensorType::Map);
	inputs.Iat = Sensor::get(SensorType::Iat);
	inputs.Pedal = Sensor::get(SensorType::AcceleratorPedal);
	inputs.TpsToleranceMin = engineConfiguration->tpsErrorDetectionTooLow;
	inputs.TpsToleranceMax = engineConfiguration->tpsErrorDetectionTooHigh;
	inputs.BarometricPressure = Sensor::get(SensorType::BarometricPressure);
	inputs.BaroFromStartup = engineConfiguration->useFixedBaroCorrFromMap;
	inputs.DriverThrottleIntent = Sensor::get(SensorType::DriverThrottleIntent);
#if EFI_IDLE_CONTROL
	inputs.IdleActive = engine->module<IdleController>()->isIdlingOrTaper();
#endif
	inputs.TemperatureSource = config->airmassTemperatureSource;
	inputs.TemperatureFallback = false;
	// Tcharge is the existing rate-limited estimate from the previous estimator
	// update. Its input fallbacks are preserved: absent CLT uses IAT; absent IAT
	// contributes 0 C; both absent yield 0 C; invalid coefficient uses CLT.
	// Airflow estimators retain their previous-calculation mass input.
	switch (inputs.TemperatureSource) {
		case AirmassTemperatureSource::Tcharge:
			inputs.TemperatureK = engine->engineState.sd.tChargeK;
			inputs.TemperatureValid = std::isfinite(inputs.TemperatureK) && inputs.TemperatureK > 0;
			break;
		case AirmassTemperatureSource::Iat:
			inputs.TemperatureK = inputs.Iat ? convertCelsiusToKelvin(inputs.Iat.Value) : 0;
			inputs.TemperatureValid = inputs.Iat.Valid && std::isfinite(inputs.TemperatureK) && inputs.TemperatureK > 0;
			break;
		default:
			inputs.TemperatureK = 0;
			inputs.TemperatureValid = false;
	}
	if (!inputs.TemperatureValid && (inputs.TemperatureSource == AirmassTemperatureSource::Tcharge ||
									 inputs.TemperatureSource == AirmassTemperatureSource::Iat)) {
		// Preserve the selected source for diagnostics, while using upstream's
		// standard IAT fallback if neither the estimator nor IAT is usable.
		const float iatK = inputs.Iat ? convertCelsiusToKelvin(inputs.Iat.Value) : 0;
		inputs.TemperatureK = std::isfinite(iatK) && iatK > 0 ? iatK : 293.15f;
		inputs.TemperatureValid = true;
		inputs.TemperatureFallback = true;
	}
	inputs.Displacement = engineConfiguration->displacement;
	inputs.CylinderCount = engine->engineState.cylinderCount;
	inputs.PreviousFuelingLoad = getFuelingLoad();
	inputs.PreviousIgnitionLoad = getIgnitionLoad();
	inputs.LambdaOverride = engineConfiguration->afrOverrideMode;
	inputs.IgnitionOverride = engineConfiguration->ignOverrideMode;
	if (resolveMap) {
		resolveCapturedMap(inputs, estimate);
	}
}

void resolveCapturedMap(AirmassInputs& inputs, const ValueProvider3D* estimate) {
	auto& map = inputs.EffectiveMap;
	map = {};
	map.Map = inputs.MeasuredMap.value_or(0);
	const bool measuredValid = inputs.MeasuredMap.Valid && std::isfinite(map.Map) && map.Map >= 0 && map.Map <= 1000;
	map.Valid = measuredValid;
	map.Fallback = !measuredValid;
	const bool transient = config->useMapEstimateTable && engineConfiguration->useMapEstimateDuringTransient &&
						   engine->module<TpsAccelEnrichment>()->isAboveAccelThreshold;
	if (!config->useMapEstimateTable || (measuredValid && !transient)) {
		return;
	}
	if (!std::isfinite(inputs.Rpm) || inputs.Rpm < 0 || !isMapEstimateConfigurationValid()) {
		map.Fallback = true;
		return;
	}
	// Match upstream's TPS-zero estimate fallback without disguising the failed
	// sensor in the shared snapshot used by Alpha-N and other consumers.
	const auto normalizedTps = normalizeAirmassPercent(inputs, inputs.Tps);
	const float estimateTps = normalizedTps.value_or(0);
	map.FallbackMap = estimate ? estimate->getValue(inputs.Rpm, estimateTps)
							   : interpolate3d(
										 config->mapEstimateTable,
										 config->mapEstimateTpsBins,
										 estimateTps,
										 config->mapEstimateRpmBins,
										 inputs.Rpm);
	map.HasValue = true;
	const bool estimateValid = std::isfinite(map.FallbackMap) && map.FallbackMap >= 0 && map.FallbackMap <= 600;
	// A broken optional transient estimate must not discard a usable MAP sensor.
	map.UsesEstimate = estimateValid && (!measuredValid || (transient && map.Map < map.FallbackMap));
	map.Map = map.UsesEstimate ? map.FallbackMap : map.Map;
	map.Valid = measuredValid || estimateValid;
	map.Fallback = !measuredValid || !estimateValid || !normalizedTps;
}

void SpeedDensityAirmass::captureInputs(float rpm, AirmassInputs& inputs) const {
	captureAirmassInputs(rpm, inputs, m_mapEstimationTable);
}

AirmassEvaluation
SpeedDensityAirmass::evaluateRawAirmass(const AirmassInputs& inputs, RawAirmassDiagnostics* diagnostics) const {
	if (diagnostics) {
		*diagnostics = {};
	}
	AirmassEvaluation evaluation;
	if (!std::isfinite(inputs.Rpm) || inputs.Rpm <= 0 || !inputs.EffectiveMap.Valid ||
		!std::isfinite(inputs.EffectiveMap.Map) || inputs.EffectiveMap.Map < 0 || inputs.EffectiveMap.Map > 1000 ||
		!inputs.TemperatureValid || !std::isfinite(inputs.TemperatureK) || inputs.TemperatureK <= 0 ||
		!std::isfinite(inputs.Displacement) || inputs.Displacement <= 0 || !std::isfinite(inputs.CylinderCount) ||
		inputs.CylinderCount <= 0) {
		return evaluation;
	}
	auto ve = evaluateRawVe(inputs, inputs.EffectiveMap.Map, diagnostics);
	const float mass = getAirmassImpl(
			ve.Ve * PERCENT_DIV,
			inputs.EffectiveMap.Map,
			inputs.TemperatureK,
			inputs.Displacement,
			inputs.CylinderCount);
	evaluation.Result = {mass, inputs.EffectiveMap.Map};
	evaluation.Degraded = inputs.TemperatureFallback || inputs.EffectiveMap.Fallback || ve.Fallback;
	evaluation.Valid = ve.Valid && std::isfinite(mass) && mass >= 0;
	return evaluation;
}

float SpeedDensityAirmass::getVeImpl(float rpm, percent_t load) const {
	return interpolate3d(config->veTable, config->veLoadBins, load, config->veRpmBins, rpm);
}
