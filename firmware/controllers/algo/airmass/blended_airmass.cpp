#include "pch.h"

#include "blended_airmass.h"
#include "alphan_airmass.h"
#include "speed_density_airmass.h"

template <typename T, size_t N>
static bool validAuthorityAxis(const T (&axis)[N], float maximum) {
	if (axis[0] < 0 || axis[N - 1] > maximum) {
		return false;
	}
	for (size_t i = 1; i < N; i++) {
		if (axis[i] <= axis[i - 1]) {
			return false;
		}
	}
	return true;
}

bool isBlendedAirmassConfigurationValid() {
	if (!isRawAirmassConfigurationValid() || !std::isfinite(engineConfiguration->displacement) ||
		engineConfiguration->displacement <= 0 || engine->engineState.cylinderCount <= 0 ||
		!validAuthorityAxis(config->airmassBlendTpsBins, 100) ||
		!validAuthorityAxis(config->airmassBlendRpmBins, 18000)) {
		return false;
	}
	for (const auto& row : config->airmassBlendTable) {
		for (auto value : row) {
			if (value > 100) {
				return false;
			}
		}
	}
	return true;
}

static float interpolateAuthority(float tps, float rpm) {
	const auto row = priv::getBin(tps, config->airmassBlendTpsBins);
	const auto column = priv::getBin(rpm, config->airmassBlendRpmBins);
	const auto& table = config->airmassBlendTable;
	// Difference form preserves flat 0/100 stencils exactly. Weighted sums can
	// round a flat 100 above its limit or below the branch-skipping endpoint.
	const auto interpolate = [](float low, float high, float fraction) { return low + (high - low) * fraction; };
	const float left = interpolate(table[row.Idx][column.Idx], table[row.Idx + 1][column.Idx], row.Frac);
	const float right = interpolate(table[row.Idx][column.Idx + 1], table[row.Idx + 1][column.Idx + 1], row.Frac);
	return interpolate(left, right, column.Frac);
}

class BlendedAirmass::DiagnosticsTarget {
public:
	explicit DiagnosticsTarget(BlendedAirmassDiagnostics* capture)
		: m_capture(capture) {
		if (capture) {
			capture->SdMass = 0;
			capture->AlphaNMass = 0;
			capture->Sd = {};
			capture->AlphaN = {};
			capture->Map = {};
			capture->RequestedAuthority = 0;
			capture->EffectiveAuthority = 0;
			capture->Corrections.HasValue = false;
			capture->Corrections.Valid = false;
			capture->Corrections.Fallback = false;
			capture->Flags = 0;
			capture->TemperatureValid = false;
			capture->TemperatureFallback = false;
			capture->BaroCoefficient = 1;
		}
	}

	explicit DiagnosticsTarget(bool postState)
		: m_postState(postState) {
		if (!postState) {
			return;
		}
		auto& output = engine->outputChannels;
		output.blendedSdMass = 0;
		output.blendedAlphaNMass = 0;
		output.blendedRequestedAuthority = 0;
		output.blendedEffectiveAuthority = 0;
		output.blendedSdLoad = 0;
		output.blendedAlphaNLoad = 0;
		output.blendedSdVe = 0;
		output.blendedAlphaNVe = 0;
		output.blendedCorrection = 0;
		output.blendedFlags = 0;
		output.fallbackMap = 0;
		// The two maps have different meanings: no single physical VE is available.
		engine->engineState.currentVe = 0;
		engine->engineState.veTableYAxis = 0;
		engine->engineState.idleVeTableYAxis = 0;
		for (size_t i = 0; i < VE_BLEND_COUNT; i++) {
			output.veBlendParameter[i] = 0;
			output.veBlendBias[i] = 0;
			output.veBlendOutput[i] = 0;
			output.veBlendYAxis[i] = 0;
		}
	}

	void map(const MapEvaluation& value) {
		m_mapFallback = value.Fallback;
		if (value.HasValue) {
			m_flags |= BlendedEstimateEvaluated;
		}
		if (value.UsesEstimate) {
			m_flags |= BlendedMapEstimateUsed;
		}
		if (m_capture) {
			m_capture->Map = value;
		}
		if (m_postState) {
			engine->outputChannels.fallbackMap =
					value.HasValue && std::isfinite(value.FallbackMap) ? value.FallbackMap : 0;
		}
	}

	void inputs(const AirmassInputs& inputs) {
		if (inputs.TemperatureFallback) {
			m_flags |= BlendedTemperatureFallback;
		}
		if (m_capture) {
			m_capture->TemperatureK = inputs.TemperatureK;
			m_capture->TemperatureValid = inputs.TemperatureValid;
			m_capture->TemperatureFallback = inputs.TemperatureFallback;
			m_capture->TemperatureSource = inputs.TemperatureSource;
			m_capture->PressureFlags = getAirmassPressureFlags(inputs, false);
		}
		if (m_postState) {
			publishAirmassTemperature(inputs);
			publishAirmassPressure(inputs, 1, false);
		}
	}

	bool consumers(const AirmassInputs& inputs, mass_t mass) {
		bool fallbackUsed = false;
		const bool valid = processAirmassConsumerLoads(inputs, mass, m_postState, &fallbackUsed);
		m_flags |= fallbackUsed ? BlendedLoadFallback : 0;
		return valid;
	}

	AirmassInjectionFault fallbackFault() const {
		if (m_flags & BlendedCorrectionFallback) {
			return AirmassInjectionFault::Correction;
		}
		if (m_flags & (BlendedTemperatureFallback | BlendedBaroFallback | BlendedIdleFallback | BlendedMapFallback)) {
			return AirmassInjectionFault::Sensor;
		}
		return AirmassInjectionFault::Load;
	}

	void authority(float requested, float effective, bool available, bool fallback) {
		m_flags |= available ? 0 : BlendedAuthorityUnavailable;
		m_flags |= fallback ? BlendedBranchFallback : 0;
		if (m_capture) {
			m_capture->RequestedAuthority = requested;
			m_capture->EffectiveAuthority = effective;
		}
		if (m_postState) {
			engine->outputChannels.blendedRequestedAuthority = requested;
			engine->outputChannels.blendedEffectiveAuthority = effective;
		}
	}

	bool degraded() const {
		return m_flags &
			   (BlendedTemperatureFallback | BlendedBranchFallback | BlendedAuthorityUnavailable | BlendedBaroFallback |
				BlendedIdleFallback | BlendedCorrectionFallback | BlendedMapFallback | BlendedLoadFallback);
	}

	void branch(bool sd, const AirmassEvaluation& value, const RawAirmassDiagnostics& raw) {
		m_flags |= raw.IdleFallback ? BlendedIdleFallback : 0;
		m_flags |= raw.BaroFallback ? BlendedBaroFallback : 0;
		m_flags |= sd ? BlendedSdEvaluated : BlendedAlphaNEvaluated;
		if (value.Valid) {
			m_flags |= sd ? BlendedSdValid : BlendedAlphaNValid;
			if (sd && m_mapFallback) {
				m_flags |= BlendedMapFallback;
			}
		}
		if (m_capture) {
			(sd ? m_capture->SdMass : m_capture->AlphaNMass) = value.Result.CylinderAirmass;
			(sd ? m_capture->Sd : m_capture->AlphaN) = raw;
			if (!sd) {
				m_capture->BaroCoefficient = raw.BaroCoefficient;
			}
		}
		if (m_postState) {
			auto& output = engine->outputChannels;
			const float mass = std::isfinite(value.Result.CylinderAirmass) ? value.Result.CylinderAirmass : 0;
			const float load =
					raw.HasValue && std::isfinite(value.Result.EngineLoadPercent) ? value.Result.EngineLoadPercent : 0;
			const float tableValue = raw.HasValue && raw.Valid ? raw.TableValue : 0;
			if (sd) {
				output.blendedSdMass = mass;
				output.blendedSdLoad = load;
				output.blendedSdVe = tableValue;
			} else {
				output.blendedAlphaNMass = mass;
				output.blendedAlphaNLoad = load;
				output.blendedAlphaNVe = tableValue;
				output.alphaNBaroCoefficient = raw.BaroCoefficient;
			}
			if (raw.IdleWeight > 0) {
				engine->engineState.idleVeTableYAxis = raw.IdleLoad;
			}
		}
	}

	VeCorrectionEvaluation corrections(const AirmassInputs& inputs) {
		auto* capture = m_capture ? &m_capture->Corrections : nullptr;
		auto value =
				m_postState ? evaluateAirmassCorrectionsForFuel(inputs) : evaluateAirmassCorrections(inputs, capture);
		m_flags |= value.Fallback ? BlendedCorrectionFallback : 0;
		if (m_postState) {
			engine->outputChannels.blendedCorrection = std::isfinite(value.Multiplier) ? value.Multiplier : 0;
		}
		return value;
	}

	void finish(bool valid) {
		if (valid) {
			m_flags |= BlendedCalculationValid;
		}
		if (m_capture) {
			m_capture->Flags = m_flags;
			if (!valid) {
				m_capture->EffectiveAuthority = 0;
			}
		}
		if (m_postState) {
			engine->outputChannels.blendedFlags = m_flags;
			if (!valid) {
				engine->outputChannels.blendedEffectiveAuthority = 0;
			}
			// Actual composite table coordinates use the per-model channels.
			engine->engineState.veTableYAxis = 0;
		}
	}

private:
	BlendedAirmassDiagnostics* m_capture = nullptr;
	uint16_t m_flags = 0;
	bool m_postState = false;
	bool m_mapFallback = false;
};

AirmassResult BlendedAirmass::getAirmass(float rpm, bool postState) {
	DiagnosticsTarget target(postState);
	return evaluateAirmass(rpm, target).Airmass.Result;
}

BlendedAirmassEvaluation BlendedAirmass::evaluateAirmass(float rpm, BlendedAirmassDiagnostics* diagnostics) const {
	DiagnosticsTarget target(diagnostics);
	return evaluateAirmass(rpm, target);
}

BlendedAirmassEvaluation BlendedAirmass::getAirmassForFuel(float rpm) const {
	DiagnosticsTarget target(true);
	return evaluateAirmass(rpm, target);
}

BlendedAirmassEvaluation BlendedAirmass::evaluateAirmass(float rpm, DiagnosticsTarget& diagnostics) const {
	BlendedAirmassEvaluation evaluation;
	const auto fail = [&](AirmassInjectionFault fault) {
		evaluation.Fault = fault;
		diagnostics.finish(false);
		return evaluation;
	};
	if (!isRawAirmassConfigurationValid()) {
		return fail(AirmassInjectionFault::Configuration);
	}
	if (!std::isfinite(rpm) || rpm <= 0) {
		return fail(AirmassInjectionFault::Sensor);
	}
	AirmassInputs inputs;
	m_sd.captureInputs(rpm, inputs);
	inputs.Composite = true;
	inputs.Model = LM_SD_ALPHA_N;
	inputs.NativeLoad = inputs.EffectiveMap.Map;
	diagnostics.inputs(inputs);
	diagnostics.map(inputs.EffectiveMap);
	const bool authorityConfigurationValid = isBlendedAirmassConfigurationValid();
	const auto normalizedTps = normalizeAirmassPercent(inputs, inputs.Tps);
	const bool authorityAvailable = authorityConfigurationValid && normalizedTps;
	const float interpolatedAuthority = authorityAvailable ? interpolateAuthority(normalizedTps.Value, inputs.Rpm) : 0;
	if (!std::isfinite(interpolatedAuthority)) {
		return fail(AirmassInjectionFault::Configuration);
	}
	// When TPS fails, SD can still run from measured MAP or the permitted
	// estimate. Alpha-N remains unavailable because its TPS input is invalid.
	const float requestedAuthority = clampF(0, interpolatedAuthority, 100);
	float authority = requestedAuthority;
	AirmassEvaluation sd;
	AirmassEvaluation alphaN;
	bool sdEvaluated = false;
	bool alphaNEvaluated = false;
	AirmassInjectionFault branchFault =
			authorityConfigurationValid ? AirmassInjectionFault::Sensor : AirmassInjectionFault::Configuration;
	const auto evaluateBranch = [&](bool speedDensity) {
		RawAirmassDiagnostics raw;
		auto value = speedDensity ? m_sd.evaluateRawAirmass(inputs, &raw)
								  : m_alphaN.evaluateRawAirmass(inputs, &raw, AlphaNPressurePolicy::PureReference);
		diagnostics.branch(speedDensity, value, raw);
		if (!value.Valid) {
			if (!isAirmassModelConfigurationValid(speedDensity ? LM_SPEED_DENSITY : LM_ALPHA_N)) {
				branchFault = AirmassInjectionFault::Configuration;
			} else if (raw.HasValue && branchFault != AirmassInjectionFault::Configuration) {
				branchFault = AirmassInjectionFault::Result;
			}
		}
		(speedDensity ? sdEvaluated : alphaNEvaluated) = true;
		(speedDensity ? sd : alphaN) = value;
	};
	if (authority < 100) {
		evaluateBranch(true);
	}
	if (authority > 0) {
		evaluateBranch(false);
	}
	bool fallback = !authorityAvailable;
	if ((authority < 100 && !sd.Valid) || (authority > 0 && !alphaN.Valid)) {
		// Evaluate an otherwise unused branch only for recovery. Never mix a
		// failed branch's numeric payload (including NaN) into a healthy mass.
		if (!sdEvaluated) {
			evaluateBranch(true);
		}
		if (!alphaNEvaluated) {
			evaluateBranch(false);
		}
		if (!sd.Valid && !alphaN.Valid) {
			diagnostics.authority(requestedAuthority, 0, authorityAvailable, true);
			return fail(branchFault);
		}
		authority = sd.Valid ? 0 : 100;
		fallback = true;
	}
	diagnostics.authority(requestedAuthority, authority, authorityAvailable, fallback);
	const float weight = authority * 0.01f;
	const float rawMass = authority == 0 ? sd.Result.CylinderAirmass
						: authority == 100
								? alphaN.Result.CylinderAirmass
								: (1 - weight) * sd.Result.CylinderAirmass + weight * alphaN.Result.CylinderAirmass;
	const auto correction = diagnostics.corrections(inputs);
	if (!correction.Valid) {
		return fail(AirmassInjectionFault::Correction);
	}
	const float mass = rawMass * correction.Multiplier;
	if (!std::isfinite(mass) || mass < 0) {
		return fail(AirmassInjectionFault::Result);
	}
	const auto filling = resolveAirmassLoad(inputs, mass, AFR_CylFilling);
	if (!filling.Valid) {
		return fail(AirmassInjectionFault::Result);
	}
	evaluation.NormalizedFilling = filling.Value;
	evaluation.LambdaLoad = resolveAirmassConsumerLoad(inputs, mass, inputs.LambdaOverride);
	evaluation.IgnitionLoad = resolveAirmassConsumerLoad(inputs, mass, inputs.IgnitionOverride);
	if (!diagnostics.consumers(inputs, mass)) {
		return fail(AirmassInjectionFault::Load);
	}
	evaluation.Airmass.Result = {mass, inputs.EffectiveMap.Valid ? inputs.EffectiveMap.Map : 0};
	evaluation.Airmass.Valid = true;
	evaluation.Degraded = diagnostics.degraded();
	evaluation.Airmass.Degraded = evaluation.Degraded;
	if (evaluation.Degraded) {
		evaluation.Fault = fallback ? branchFault : diagnostics.fallbackFault();
	}
	diagnostics.finish(true);
	return evaluation;
}
