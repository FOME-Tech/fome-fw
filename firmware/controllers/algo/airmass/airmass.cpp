#include "pch.h"

#include "airmass.h"
#include "idle_thread.h"
#include "gppwm_channel.h"
#include "speed_density_base.h"

static VeCorrectionEvaluation evaluateAirmassCorrectionsImpl(
		const AirmassInputs& inputs,
		VeCorrectionDiagnostics* diagnostics,
		bool postState,
		VeDiagnostics* standalone = nullptr);

AirmassVeModelBase::AirmassVeModelBase(const ValueProvider3D* veTable, engine_load_mode_e model)
	: m_veTable(veTable)
	, m_model(model) {}

template <typename T, size_t N>
static bool isAxisValid(const T (&axis)[N], float maximum, float minimum = 0) {
	// Packed integer storage guarantees finite values. With a strictly ascending
	// axis, checking the first and last bins bounds the whole axis.
	if (axis[0] < minimum || axis[N - 1] > maximum) {
		return false;
	}
	for (size_t i = 1; i < N; i++) {
		if (axis[i] <= axis[i - 1]) {
			return false;
		}
	}
	return true;
}

bool isAirmassModelConfigurationValid(engine_load_mode_e model) {
	switch (model) {
		case LM_SPEED_DENSITY:
			return isAxisValid(config->veLoadBins, 1000) && isAxisValid(config->veRpmBins, 18000);
		case LM_ALPHA_N:
			return isAxisValid(config->alphaNTpsBins, 100) && isAxisValid(config->alphaNRpmBins, 18000);
		case LM_REAL_MAF:
			return isAxisValid(config->mafLoadBins, 1000) && isAxisValid(config->mafRpmBins, 18000);
		default:
			return true;
	}
}

bool isAirmassConfigurationValid() {
	return isAirmassModelConfigurationValid(engineConfiguration->fuelAlgorithm);
}

bool isMapEstimateAxesValid() {
	return isAxisValid(config->mapEstimateTpsBins, 100) && isAxisValid(config->mapEstimateRpmBins, 18000);
}

bool isMapEstimateConfigurationValid() {
	if (!isMapEstimateAxesValid()) {
		return false;
	}
	for (const auto& row : config->mapEstimateTable) {
		for (float value : row) {
			if (!std::isfinite(value) || value < 0 || value > 600) {
				return false;
			}
		}
	}
	return true;
}

bool isRawAirmassConfigurationValid() {
	// Model-specific axes are checked only when that branch contributes.
	return std::isfinite(engineConfiguration->displacement) && engineConfiguration->displacement > 0 &&
		   engine->engineState.cylinderCount > 0;
}

bool validateAirmassConfiguration() {
	if (!isAirmassConfigurationValid()) {
		firmwareError(ObdCode::CUSTOM_ERR_ASSERT, "Invalid active airmass table axes");
		return false;
	}
	return true;
}

float AirmassVeModelBase::getVe(float rpm, float load, bool postState) const {
	auto evaluation = evaluateVe(rpm, load, DiagnosticsTarget(postState));
	return evaluation.Ve * PERCENT_DIV;
}

VeEvaluation AirmassVeModelBase::evaluateVe(float rpm, float load, VeDiagnostics* diagnostics) const {
	return evaluateVe(rpm, load, DiagnosticsTarget(diagnostics));
}

VeEvaluation AirmassVeModelBase::evaluateVe(float rpm, float load, const DiagnosticsTarget& diagnostics) const {
	AirmassInputs inputs;
	captureAirmassInputs(rpm, inputs);
	inputs.NativeLoad = load;
	inputs.Model = m_model;
	return evaluateVe(inputs, load, diagnostics);
}

VeEvaluation
AirmassVeModelBase::evaluateVe(const AirmassInputs& inputs, float load, const DiagnosticsTarget& diagnostics) const {
	RawAirmassDiagnostics raw;
	auto evaluation = evaluateRawVe(inputs, load, &raw);
	if (!raw.HasValue) {
		return evaluation;
	}
	// Standalone corrections retain their native main-table load when Default is
	// selected. Composite corrections run once after the two masses are blended.
	const auto corrections = diagnostics.corrections(inputs);
	evaluation.Ve *= corrections.Multiplier;
	evaluation.Fallback = evaluation.Fallback || corrections.Fallback;
	evaluation.Valid = evaluation.Valid && corrections.Valid && std::isfinite(evaluation.Ve) && evaluation.Ve >= 0;
	diagnostics.ve(evaluation, load, raw.IdleLoad);
	return evaluation;
}

static void publishBlend(size_t index, const BlendResult& result) {
	engine->outputChannels.veBlendParameter[index] = result.BlendParameter;
	engine->outputChannels.veBlendBias[index] = result.Bias;
	engine->outputChannels.veBlendOutput[index] = result.Value;
	engine->outputChannels.veBlendYAxis[index] = result.TableYAxis;
}

static void publishVeValues(percent_t ve, float load, float idleLoad) {
	engine->engineState.currentVe = ve;
	engine->engineState.veTableYAxis = load;
	engine->engineState.idleVeTableYAxis = idleLoad;
}

static void publishMap(const MapEvaluation& result) {
#if EFI_TUNER_STUDIO
	if (result.HasValue) {
		engine->outputChannels.fallbackMap = result.FallbackMap;
	}
#else
	(void)result;
#endif
}

AirmassVeModelBase::DiagnosticsTarget::DiagnosticsTarget(bool postState)
	: m_postState(postState) {}

AirmassVeModelBase::DiagnosticsTarget::DiagnosticsTarget(VeDiagnostics* diagnostics)
	: m_ve(diagnostics) {
	if (m_ve) {
		m_ve->HasValue = false;
		m_ve->Valid = false;
	}
}

AirmassVeModelBase::DiagnosticsTarget::DiagnosticsTarget(AirmassDiagnostics* diagnostics)
	: DiagnosticsTarget(diagnostics ? &diagnostics->Ve : nullptr) {
	if (diagnostics) {
		m_airmass = diagnostics;
		diagnostics->TemperatureValid = false;
		diagnostics->TemperatureFallback = false;
		diagnostics->BaroCoefficient = 1;
		diagnostics->PressureFlags = 0;
		m_map = &diagnostics->Map;
		m_map->HasValue = false;
		m_map->Valid = false;
	}
}

void AirmassVeModelBase::DiagnosticsTarget::blend(size_t index, const BlendResult& result) const {
	if (m_ve) {
		m_ve->Blends[index] = result;
	}
	if (m_postState) {
		publishBlend(index, result);
	}
}

void AirmassVeModelBase::DiagnosticsTarget::ve(const VeEvaluation& result, float load, float idleLoad) const {
	if (m_ve) {
		m_ve->Ve = result.Ve;
		m_ve->Load = load;
		m_ve->IdleLoad = idleLoad;
		m_ve->Valid = result.Valid;
		m_ve->HasValue = true;
	}
	if (m_postState) {
		publishVeValues(result.Ve, load, idleLoad);
	}
}

void AirmassVeModelBase::DiagnosticsTarget::map(const MapEvaluation& result) const {
	if (m_map) {
		*m_map = result;
	}
	if (m_postState) {
		publishMap(result);
	}
}

uint8_t getAirmassPressureFlags(const AirmassInputs& inputs, bool multiplyMap) {
	const bool valid = inputs.BarometricPressure && std::isfinite(inputs.BarometricPressure.Value) &&
					   inputs.BarometricPressure.Value > 0 && inputs.BarometricPressure.Value <= 200;
	return (valid ? (inputs.BaroFromStartup ? 2 : 1) : 4) | (multiplyMap ? 8 : 0);
}

void publishAirmassTemperature(const AirmassInputs& inputs) {
	engine->outputChannels.airmassTemperature = inputs.TemperatureValid ? inputs.TemperatureK - 273.15f : 0;
	engine->outputChannels.airmassTemperatureSourceUsed = static_cast<uint8_t>(inputs.TemperatureSource);
}

void publishAirmassPressure(const AirmassInputs& inputs, float coefficient, bool multiplyMap) {
	engine->outputChannels.alphaNBaroCoefficient = coefficient;
	engine->outputChannels.airmassPressureFlags = getAirmassPressureFlags(inputs, multiplyMap);
}

void AirmassVeModelBase::DiagnosticsTarget::temperature(const AirmassInputs& inputs) const {
	if (m_airmass) {
		m_airmass->TemperatureK = inputs.TemperatureK;
		m_airmass->TemperatureValid = inputs.TemperatureValid;
		m_airmass->TemperatureFallback = inputs.TemperatureFallback;
		m_airmass->TemperatureSource = inputs.TemperatureSource;
	}
	if (m_postState) {
		publishAirmassTemperature(inputs);
	}
}

void AirmassVeModelBase::DiagnosticsTarget::pressure(
		const AirmassInputs& inputs, float coefficient, bool multiplyMap) const {
	if (m_airmass) {
		m_airmass->BaroCoefficient = coefficient;
		m_airmass->PressureFlags = getAirmassPressureFlags(inputs, multiplyMap);
	}
	if (m_postState) {
		publishAirmassPressure(inputs, coefficient, multiplyMap);
	}
}

bool AirmassVeModelBase::DiagnosticsTarget::consumers(const AirmassInputs& inputs, mass_t mass) const {
	return processAirmassConsumerLoads(inputs, mass, m_postState);
}

VeCorrectionEvaluation AirmassVeModelBase::DiagnosticsTarget::corrections(const AirmassInputs& inputs) const {
	return evaluateAirmassCorrectionsImpl(inputs, nullptr, m_postState, m_ve);
}

void AirmassVeModelBase::publishVe(const VeDiagnostics& diagnostics) {
	if (!diagnostics.HasValue) {
		return;
	}

	for (size_t i = 0; i < efi::size(diagnostics.Blends); i++) {
		publishBlend(i, diagnostics.Blends[i]);
	}

	publishVeValues(diagnostics.Ve, diagnostics.Load, diagnostics.IdleLoad);
}

void AirmassVeModelBase::publishEvaluation(const AirmassDiagnostics& diagnostics) {
	engine->outputChannels.airmassTemperature = diagnostics.TemperatureValid ? diagnostics.TemperatureK - 273.15f : 0;
	engine->outputChannels.airmassTemperatureSourceUsed = static_cast<uint8_t>(diagnostics.TemperatureSource);
	engine->outputChannels.alphaNBaroCoefficient = diagnostics.BaroCoefficient;
	engine->outputChannels.airmassPressureFlags = diagnostics.PressureFlags;
	publishMap(diagnostics.Map);
	publishVe(diagnostics.Ve);
}

float AirmassVeModelBase::getVeImpl(float rpm, percent_t load) const {
	return interpolate3d(config->veTable, config->veLoadBins, load, config->veRpmBins, rpm);
}

float AirmassVeModelBase::getDedicatedVeImpl(float rpm, float load) const {
	return getVeImpl(rpm, load);
}

VeEvaluation
AirmassVeModelBase::evaluateRawVe(const AirmassInputs& inputs, float load, RawAirmassDiagnostics* diagnostics) const {
	if (!isAirmassModelConfigurationValid(m_model) || !std::isfinite(inputs.Rpm) || !std::isfinite(load)) {
		return {};
	}
	float value = m_veTable ? m_veTable->getValue(inputs.Rpm, load) : getDedicatedVeImpl(inputs.Rpm, load);
	bool valid = std::isfinite(value) && value >= 0;
	float idleLoad = load;
	float idleWeight = 0;
	bool idleFallback = inputs.Composite && inputs.IdleActive && engineConfiguration->useSeparateVeForIdle &&
						config->idleVeModel != IdleVeModel::SpeedDensity && config->idleVeModel != IdleVeModel::AlphaN;
	const bool ownsIdle = !inputs.Composite ||
						  (m_model == LM_SPEED_DENSITY && config->idleVeModel == IdleVeModel::SpeedDensity) ||
						  (m_model == LM_ALPHA_N && config->idleVeModel == IdleVeModel::AlphaN);
	if (engineConfiguration->useSeparateVeForIdle && inputs.IdleActive && ownsIdle) {
		const float threshold = engineConfiguration->idlePidDeactivationTpsThreshold;
		if (!inputs.DriverThrottleIntent || !std::isfinite(inputs.DriverThrottleIntent.Value) ||
			!std::isfinite(threshold) || threshold <= 0) {
			idleFallback = true;
		} else {
			idleWeight = interpolateClamped(threshold / 2, 1, threshold, 0, inputs.DriverThrottleIntent.Value);
			// At zero weight the idle calibration and its input are not dependencies.
			if (idleWeight > 0) {
				AirmassLoad source;
				switch (config->idleVeLoadSource) {
					case IdleVeLoadSource::ModelDefault:
						// Explicit native-model option, independent of other consumers.
						source.Value = load;
						source.Valid = true;
						break;
					case IdleVeLoadSource::MeasuredMap:
						source = resolveAirmassLoad(inputs, 0, AFR_MAP);
						break;
					case IdleVeLoadSource::Tps:
						source = resolveAirmassLoad(inputs, 0, AFR_Tps);
						break;
					case IdleVeLoadSource::EffectiveMap:
						source = resolveAirmassLoad(inputs, 0, AFR_EffectiveMAP);
						break;
					default:
						break;
				}
				idleLoad = source.Value;
				const bool idleValid = source.Valid && isAxisValid(config->idleVeLoadBins, 1000) &&
									   isAxisValid(config->idleVeRpmBins, 18000);
				idleFallback = !idleValid;
				if (valid && idleValid) {
					const float idle = interpolate3d(
							config->idleVeTable, config->idleVeLoadBins, idleLoad, config->idleVeRpmBins, inputs.Rpm);
					idleFallback = !std::isfinite(idle) || idle < 0;
					if (!idleFallback) {
						value += idleWeight * (idle - value);
					}
				}
			}
		}
	}
	if (idleFallback) {
		idleWeight = 0;
		idleLoad = load;
	}
	VeEvaluation evaluation{value, valid && std::isfinite(value) && value >= 0, idleFallback};
	if (diagnostics) {
		diagnostics->IdleFallback = idleFallback;
		diagnostics->TableValue = value;
		diagnostics->IdleLoad = idleLoad;
		diagnostics->IdleWeight = idleWeight;
		diagnostics->HasValue = true;
		diagnostics->Valid = evaluation.Valid;
	}
	return evaluation;
}

AirmassLoad resolveAirmassLoad(const AirmassInputs& inputs, mass_t finalMass, load_override_e selector) {
	AirmassLoad result;
	expected<float> sensor = unexpected;
	float maximum = 100;
	switch (selector) {
		case AFR_None:
			if (!inputs.Composite) {
				result.Value = inputs.NativeLoad;
				result.Source = inputs.Model == LM_ALPHA_N	? AirmassLoadSource::Tps
							  : inputs.Model == LM_REAL_MAF ? AirmassLoadSource::CylinderFilling
															: AirmassLoadSource::EffectiveMap;
				result.Unit = inputs.Model == LM_SPEED_DENSITY ? AirmassLoadUnit::Kpa : AirmassLoadUnit::Percent;
				result.Valid = std::isfinite(result.Value) && result.Value >= 0;
				if (inputs.Model == LM_SPEED_DENSITY) {
					result.Valid = result.Valid && inputs.EffectiveMap.Valid &&
								   std::isfinite(inputs.EffectiveMap.Map) && inputs.EffectiveMap.Map >= 0 &&
								   inputs.EffectiveMap.Map <= 1000;
					result.UsesEstimate = inputs.EffectiveMap.UsesEstimate;
				} else if (inputs.Model == LM_ALPHA_N) {
					const auto sensorTps = normalizeAirmassPercent(inputs, inputs.Tps);
					const auto tps = normalizeAirmassPercent(inputs, expected<float>(result.Value));
					result.Valid = sensorTps && tps;
					if (tps) {
						result.Value = tps.Value;
					}
				}
				return result;
			}
			[[fallthrough]];
		case AFR_EffectiveMAP:
			result.Value = inputs.EffectiveMap.Map;
			result.Source = AirmassLoadSource::EffectiveMap;
			result.Unit = AirmassLoadUnit::Kpa;
			result.Valid = inputs.EffectiveMap.Valid && std::isfinite(result.Value) && result.Value >= 0 &&
						   result.Value <= 1000;
			result.UsesEstimate = inputs.EffectiveMap.UsesEstimate;
			return result;
		case AFR_MAP:
			sensor = inputs.MeasuredMap;
			result.Source = AirmassLoadSource::MeasuredMap;
			result.Unit = AirmassLoadUnit::Kpa;
			maximum = 1000;
			break;
		case AFR_Tps:
			sensor = normalizeAirmassPercent(inputs, inputs.Tps);
			result.Source = AirmassLoadSource::Tps;
			break;
		case AFR_AccPedal:
			sensor = normalizeAirmassPercent(inputs, inputs.Pedal);
			result.Source = AirmassLoadSource::Pedal;
			break;
		case AFR_CylFilling: {
			result.Source = AirmassLoadSource::CylinderFilling;
			if (!std::isfinite(finalMass) || finalMass < 0 || !std::isfinite(inputs.Displacement) ||
				inputs.Displacement <= 0 || !std::isfinite(inputs.CylinderCount) || inputs.CylinderCount <= 0) {
				return result;
			}
			const float standardCharge = idealGasLaw(inputs.Displacement / inputs.CylinderCount, 101.325f, 293.15f);
			result.Value = 100 * finalMass / standardCharge;
			result.Valid = std::isfinite(result.Value) && result.Value >= 0;
			return result;
		}
		default:
			return result;
	}
	result.Value = sensor.value_or(0);
	result.Valid = sensor.Valid && std::isfinite(result.Value) && result.Value >= 0 && result.Value <= maximum;
	return result;
}

expected<float> normalizeAirmassPercent(const AirmassInputs& inputs, expected<float> sensor) {
	if (!sensor || !std::isfinite(sensor.Value)) {
		return unexpected;
	}
	if (!std::isfinite(inputs.TpsToleranceMin) || !std::isfinite(inputs.TpsToleranceMax) ||
		inputs.TpsToleranceMin > inputs.TpsToleranceMax || sensor.Value < inputs.TpsToleranceMin ||
		sensor.Value > inputs.TpsToleranceMax) {
		return unexpected;
	}
	return clampF(0, sensor.Value, 100);
}

AirmassLoad resolveAirmassConsumerLoad(const AirmassInputs& inputs, mass_t finalMass, load_override_e selector) {
	auto result = resolveAirmassLoad(inputs, finalMass, selector);
	if (result.Valid || !std::isfinite(finalMass) || finalMass < 0) {
		return result;
	}
	switch (result.Source) {
		case AirmassLoadSource::EffectiveMap:
		case AirmassLoadSource::MeasuredMap:
			result.Value = 200;
			break;
		case AirmassLoadSource::Tps:
		case AirmassLoadSource::Pedal:
			result.Value = 100;
			break;
		default:
			return result;
	}
	result.Valid = true;
	result.UsesEstimate = false;
	return result;
}

namespace {
expected<float> boundedInput(expected<float> value, float maximum) {
	return value.Valid && std::isfinite(value.Value) && value.Value >= 0 && value.Value <= maximum
				 ? value
				 : expected<float>(unexpected);
}

// At most two input channels per correction. This local cache is shared by
// standalone and composite correction passes. NaN encodes an invalid captured
// channel; all valid correction inputs must be finite.
class CorrectionChannels {
public:
	explicit CorrectionChannels(const AirmassInputs& inputs)
		: m_inputs(inputs) {}

	expected<float> read(gppwm_channel_e channel) {
		switch (channel) {
			case GPPWM_Zero:
				return 0;
			case GPPWM_Rpm:
				return m_inputs.Rpm;
			case GPPWM_Tps:
				return normalizeAirmassPercent(m_inputs, m_inputs.Tps);
			case GPPWM_EffectiveMap:
				return m_inputs.EffectiveMap.Valid ? boundedInput(m_inputs.EffectiveMap.Map, 1000)
												   : expected<float>(unexpected);
			case GPPWM_Map:
				return boundedInput(m_inputs.MeasuredMap, 1000);
			case GPPWM_BaroPressure:
				return m_inputs.BarometricPressure;
			case GPPWM_Iat:
				return m_inputs.Iat;
			case GPPWM_AccelPedal:
				return normalizeAirmassPercent(m_inputs, m_inputs.Pedal);
			case GPPWM_FuelLoad:
				return m_inputs.PreviousFuelingLoad;
			case GPPWM_IgnLoad:
				return m_inputs.PreviousIgnitionLoad;
			default:
				break;
		}
		for (size_t i = 0; i < m_count; i++) {
			if (m_channels[i] == channel) {
				return std::isfinite(m_values[i]) ? expected<float>(m_values[i]) : expected<float>(unexpected);
			}
		}
		// The legacy GPPWM vehicle-speed reader substitutes zero on failure. The
		// strict path needs the sensor's validity, just like other sensor channels.
		// Derived EGT/GPPWM outputs expose no source validity through this API:
		// their contract is a finite sampled value, not proven sensor health.
		auto value = channel == GPPWM_VehicleSpeed ? Sensor::get(SensorType::VehicleSpeed) : readGppwmChannel(channel);
		m_channels[m_count] = channel;
		m_values[m_count++] = value.value_or(NAN);
		return value;
	}

private:
	const AirmassInputs& m_inputs;
	float m_values[VE_BLEND_COUNT * 2];
	gppwm_channel_e m_channels[VE_BLEND_COUNT * 2];
	size_t m_count = 0;
};
} // namespace

static VeCorrectionEvaluation evaluateAirmassCorrectionsImpl(
		const AirmassInputs& inputs, VeCorrectionDiagnostics* diagnostics, bool postState, VeDiagnostics* standalone) {
	if (diagnostics) {
		diagnostics->HasValue = false;
		diagnostics->Valid = false;
		diagnostics->Fallback = false;
	}
	VeCorrectionEvaluation evaluation{1, true};
	CorrectionChannels channels(inputs);
	for (size_t i = 0; i < efi::size(config->veBlends); i++) {
		const auto& cfg = config->veBlends[i];
		BlendResult result{};
		if (cfg.blendParameter != GPPWM_Zero) {
			auto parameter = channels.read(cfg.blendParameter);
			auto native = resolveAirmassLoad(inputs, 0, AFR_None);
			auto load = cfg.yAxisOverride == GPPWM_Zero
							  ? (native.Valid ? expected<float>(native.Value) : expected<float>(unexpected))
							  : channels.read(cfg.yAxisOverride);
			if (!parameter || !load || !std::isfinite(parameter.Value) || !std::isfinite(load.Value) ||
				!std::isfinite(inputs.Rpm) || inputs.Rpm <= 0 || !isAxisValid(cfg.loadBins, 1000) ||
				!isAxisValid(cfg.rpmBins, 18000) || !isAxisValid(cfg.blendBins, 1000, -1000)) {
				evaluation.Fallback = true;
			} else {
				result = calculateBlend(cfg, inputs.Rpm, load.Value, parameter.Value);
				const float factor = applyVeCorrection(1, result.Value);
				const float corrected = applyVeCorrection(evaluation.Multiplier, result.Value);
				if (std::isfinite(result.Value) && std::isfinite(factor) && factor >= 0 && std::isfinite(corrected) &&
					corrected >= 0) {
					evaluation.Multiplier = corrected;
				} else {
					evaluation.Fallback = true;
					result = {};
				}
			}
		}
		if (diagnostics) {
			diagnostics->Blends[i] = result;
		}
		if (standalone) {
			standalone->Blends[i] = result;
		}
		if (postState) {
			publishBlend(i, result);
		}
	}
	evaluation.Valid = evaluation.Valid && std::isfinite(evaluation.Multiplier) && evaluation.Multiplier >= 0;
	if (diagnostics) {
		diagnostics->HasValue = true;
		diagnostics->Valid = evaluation.Valid;
		diagnostics->Fallback = evaluation.Fallback;
	}
	return evaluation;
}

VeCorrectionEvaluation evaluateAirmassCorrections(const AirmassInputs& inputs, VeCorrectionDiagnostics* diagnostics) {
	return evaluateAirmassCorrectionsImpl(inputs, diagnostics, false);
}

VeCorrectionEvaluation evaluateAirmassCorrectionsForFuel(const AirmassInputs& inputs) {
	return evaluateAirmassCorrectionsImpl(inputs, nullptr, true);
}
