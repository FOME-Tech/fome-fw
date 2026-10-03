#include "pch.h"

#include "alphan_airmass.h"

namespace {
expected<float>
alphaNPressure(const AirmassInputs& inputs, AlphaNPressurePolicy policy, float& coefficient, bool& fallback) {
	coefficient = 1;
	fallback = false;
	if (policy == AlphaNPressurePolicy::EffectiveMap) {
		return inputs.EffectiveMap.Valid && std::isfinite(inputs.EffectiveMap.Map) && inputs.EffectiveMap.Map >= 0 &&
							   inputs.EffectiveMap.Map <= 1000
					 ? expected<float>(inputs.EffectiveMap.Map)
					 : expected<float>(unexpected);
	}
	if (config->alphaNBaroCompensation) {
		const float reference = config->alphaNBaroReferencePressure;
		if (!std::isfinite(reference) || reference <= 0 || !inputs.BarometricPressure ||
			!std::isfinite(inputs.BarometricPressure.Value) || inputs.BarometricPressure.Value <= 0 ||
			inputs.BarometricPressure.Value > 200) {
			fallback = true;
			return 101.325f;
		}
		coefficient = inputs.BarometricPressure.Value / reference;
		if (!std::isfinite(coefficient) || coefficient <= 0 || !std::isfinite(101.325f * coefficient)) {
			coefficient = 1;
			fallback = true;
		}
	}
	return 101.325f * coefficient;
}

bool validAlphaNInputs(const AirmassInputs& inputs) {
	return std::isfinite(inputs.Rpm) && inputs.Rpm > 0 && normalizeAirmassPercent(inputs, inputs.Tps) &&
		   inputs.TemperatureValid && std::isfinite(inputs.TemperatureK) && inputs.TemperatureK > 0 &&
		   std::isfinite(inputs.Displacement) && inputs.Displacement > 0 && std::isfinite(inputs.CylinderCount) &&
		   inputs.CylinderCount > 0;
}
} // namespace

AirmassResult AlphaNAirmass::getAirmass(float rpm, bool postState) {
	return evaluateAirmass(rpm, DiagnosticsTarget(postState)).Result;
}

AirmassEvaluation AlphaNAirmass::getAirmassForFuel(float rpm) const {
	return evaluateAirmass(rpm, DiagnosticsTarget(true));
}

AirmassEvaluation AlphaNAirmass::evaluateAirmass(float rpm, AirmassDiagnostics* diagnostics) const {
	return evaluateAirmass(rpm, DiagnosticsTarget(diagnostics));
}

AirmassEvaluation
AlphaNAirmass::evaluateAirmass(float rpm, AlphaNPressurePolicy policy, AirmassDiagnostics* diagnostics) const {
	return evaluateAirmass(rpm, policy, DiagnosticsTarget(diagnostics));
}

AirmassEvaluation AlphaNAirmass::evaluateAirmass(float rpm, const DiagnosticsTarget& diagnostics) const {
	return evaluateAirmass(
			rpm,
			config->alphaNMultiplyMap ? AlphaNPressurePolicy::EffectiveMap : AlphaNPressurePolicy::PureReference,
			diagnostics);
}

AirmassEvaluation
AlphaNAirmass::evaluateAirmass(float rpm, AlphaNPressurePolicy policy, const DiagnosticsTarget& diagnostics) const {
	AirmassInputs inputs;
	captureAirmassInputs(rpm, inputs, nullptr, true, diagnostics.live());
	const auto tps = normalizeAirmassPercent(inputs, inputs.Tps);
	inputs.NativeLoad = tps.value_or(0);
	inputs.Model = LM_ALPHA_N;
	// This wrapper explicitly requests standalone physics. Composite and dry raw
	// callers supply their pressure policy directly, independent of global mode.
	float coefficient;
	bool pressureFallback;
	const auto pressure = alphaNPressure(inputs, policy, coefficient, pressureFallback);
	diagnostics.map(inputs.EffectiveMap);
	diagnostics.temperature(inputs);
	diagnostics.pressure(inputs, coefficient, policy == AlphaNPressurePolicy::EffectiveMap);
	AirmassEvaluation evaluation;
	if (!validAlphaNInputs(inputs) || !pressure) {
		return evaluation;
	}
	const auto ve = evaluateVe(inputs, tps.Value, diagnostics);
	const float mass = getAirmassImpl(
			ve.Ve * PERCENT_DIV, pressure.Value, inputs.TemperatureK, inputs.Displacement, inputs.CylinderCount);
	evaluation.Result = {mass, tps.Value};
	evaluation.Degraded = inputs.TemperatureFallback || pressureFallback || ve.Fallback ||
						  (policy == AlphaNPressurePolicy::EffectiveMap && inputs.EffectiveMap.Fallback);
	evaluation.Valid = ve.Valid && std::isfinite(mass) && mass >= 0;
	if (evaluation.Valid) {
		evaluation.Valid = diagnostics.consumers(inputs, mass);
	}
	if (!evaluation.Valid) {
		evaluation.Result.CylinderAirmass = 0;
	}
	return evaluation;
}

float AlphaNAirmass::getVeImpl(float rpm, percent_t load) const {
	return getDedicatedVeImpl(rpm, load);
}

float AlphaNAirmass::getDedicatedVeImpl(float rpm, float load) const {
	return interpolate3d(config->alphaNTable, config->alphaNTpsBins, load, config->alphaNRpmBins, rpm);
}

AirmassEvaluation AlphaNAirmass::evaluateRawAirmass(
		const AirmassInputs& inputs, RawAirmassDiagnostics* diagnostics, AlphaNPressurePolicy pressurePolicy) const {
	return evaluateRawAirmassImpl(inputs, diagnostics, pressurePolicy, false);
}

AirmassEvaluation AlphaNAirmass::evaluateRawAirmassImpl(
		const AirmassInputs& inputs,
		RawAirmassDiagnostics* diagnostics,
		AlphaNPressurePolicy pressurePolicy,
		bool liveCalibration) const {
	if (diagnostics) {
		*diagnostics = {};
	}
	AirmassEvaluation evaluation;
	if (!validAlphaNInputs(inputs)) {
		return evaluation;
	}
	const auto tps = normalizeAirmassPercent(inputs, inputs.Tps);
	float coefficient;
	bool pressureFallback;
	const auto pressure = alphaNPressure(inputs, pressurePolicy, coefficient, pressureFallback);
	if (diagnostics) {
		diagnostics->BaroCoefficient = coefficient;
		diagnostics->BaroFallback = pressureFallback;
	}
	if (!pressure) {
		return evaluation;
	}
	const auto ve = evaluateRawVe(inputs, tps.Value, diagnostics, liveCalibration);
	const float mass = getAirmassImpl(
			ve.Ve * PERCENT_DIV, pressure.Value, inputs.TemperatureK, inputs.Displacement, inputs.CylinderCount);
	evaluation.Result = {mass, tps.Value};
	evaluation.Degraded = inputs.TemperatureFallback || pressureFallback || ve.Fallback ||
						  (pressurePolicy == AlphaNPressurePolicy::EffectiveMap && inputs.EffectiveMap.Fallback);
	evaluation.Valid = ve.Valid && std::isfinite(mass) && mass >= 0;
	return evaluation;
}
