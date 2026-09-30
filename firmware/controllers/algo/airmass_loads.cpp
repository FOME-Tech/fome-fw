#include "pch.h"
#include "airmass_loads.h"
#include "airmass.h"
#include "fuel_math.h"
#include "closed_loop_fuel.h"

namespace {
bool revisedModel() {
	const auto mode = engineConfiguration->fuelAlgorithm;
	return mode == LM_SPEED_DENSITY || mode == LM_ALPHA_N || mode == LM_REAL_MAF || mode == LM_SD_ALPHA_N;
}
load_override_e selector(AirmassConsumer consumer, size_t index) {
	switch (consumer) {
		case AirmassConsumer::InjectionPhase:
			return config->injectionPhaseLoadSource;
		case AirmassConsumer::FuelTrim:
			return config->fuelTrimLoadSource[index];
		case AirmassConsumer::IgnitionTrim:
			return config->ignitionTrimLoadSource[index];
		case AirmassConsumer::Stft:
			return config->stftLoadSource;
		case AirmassConsumer::Staging:
			return config->stagingLoadSource;
		case AirmassConsumer::LambdaDeviation:
			return config->lambdaDeviationLoadSource;
		case AirmassConsumer::LambdaMonitor:
			return config->lambdaMonitorLoadSource;
		case AirmassConsumer::TrailingSpark:
			return config->trailingSparkLoadSource;
		case AirmassConsumer::IgnitionIat:
			return config->ignitionIatLoadSource;
		case AirmassConsumer::KnockRetard:
			return config->knockRetardLoadSource;
		case AirmassConsumer::KnockGain:
			return config->knockGainLoadSource[index];
		case AirmassConsumer::HpfpTarget:
			return config->hpfpTargetLoadSource;
		default:
			return static_cast<load_override_e>(255);
	}
}
bool cylinderConsumer(AirmassConsumer consumer) {
	return consumer == AirmassConsumer::FuelTrim || consumer == AirmassConsumer::IgnitionTrim ||
		   consumer == AirmassConsumer::KnockGain;
}
float consumerFromSnapshot(const AirmassLoadSnapshot& s, load_override_e source);
bool active(AirmassConsumer consumer, const AirmassInputs& inputs, const AirmassLoadSnapshot& snapshot) {
	switch (consumer) {
		case AirmassConsumer::InjectionPhase:
		case AirmassConsumer::FuelTrim:
			return engineConfiguration->isInjectionEnabled;
		case AirmassConsumer::Stft: {
#if EFI_SHAFT_POSITION_INPUT
			const auto clt = Sensor::get(SensorType::Clt);
			const auto& stft = engineConfiguration->stft;
			return engineConfiguration->fuelClosedLoopCorrectionEnabled && engine->rpmCalculator.isRunning() && clt &&
				   clt.Value >= stft.minClt &&
				   engine->fuelComputer.running.timeSinceCrankingInSecs >= stft.startupDelay;
#else
			return false;
#endif
		}
		case AirmassConsumer::Staging:
			return engineConfiguration->isInjectionEnabled && engineConfiguration->enableStagedInjection;
		case AirmassConsumer::LambdaDeviation:
		case AirmassConsumer::LambdaMonitor: {
			if (!engineConfiguration->lambdaProtectionEnable) {
				return false;
			}
			if (consumer == AirmassConsumer::LambdaMonitor && engine->lambdaMonitor.isCut()) {
				return true;
			}
			if (inputs.Rpm < engineConfiguration->lambdaProtectionMinRpm || !inputs.Tps ||
				inputs.Tps.Value <= engineConfiguration->lambdaProtectionMinTps ||
				engine->module<DfcoController>()->getTimeSinceCut() < engineConfiguration->noFuelTrimAfterDfcoTime ||
				engine->module<LimpManager>()->getTimeSinceAnyCut() < 2) {
				return false;
			}
			if (consumer == AirmassConsumer::LambdaMonitor) {
				return true;
			}
			const float load = consumerFromSnapshot(snapshot, config->lambdaMonitorLoadSource);
			return std::isfinite(load) && load >= engineConfiguration->lambdaProtectionMinLoad &&
				   Sensor::get(SensorType::Lambda1).Valid;
		}
		case AirmassConsumer::IgnitionTrim:
			return engineConfiguration->isIgnitionEnabled;
		case AirmassConsumer::IgnitionIat:
			return engineConfiguration->isIgnitionEnabled && inputs.Iat &&
				   engineConfiguration->timingMode == TM_DYNAMIC &&
				   (!engine->rpmCalculator.isCranking() || engineConfiguration->useAdvanceCorrectionsForCranking);
		case AirmassConsumer::TrailingSpark:
			return engineConfiguration->isIgnitionEnabled && engineConfiguration->enableTrailingSparks;
		case AirmassConsumer::KnockGain:
		case AirmassConsumer::KnockRetard:
			return engineConfiguration->enableSoftwareKnock && engine->rpmCalculator.isRunning();
		case AirmassConsumer::HpfpTarget:
			return inputs.Rpm >= 60 && enginePins.hpfpValve.isInitialized() && engineConfiguration->hpfpCamLobes &&
				   engineConfiguration->hpfpPumpVolume > 0;
		default:
			return false;
	}
}
bool lambdaTargetRequired(const AirmassInputs& inputs, const AirmassLoadSnapshot& snapshot) {
	if (engineConfiguration->isInjectionEnabled || active(AirmassConsumer::LambdaDeviation, inputs, snapshot)) {
		return true;
	}
	if (active(AirmassConsumer::Stft, inputs, snapshot)) {
		for (const auto sensor : {SensorType::Lambda1, SensorType::Lambda2, SensorType::Lambda3, SensorType::Lambda4}) {
			if (Sensor::get(sensor).Valid && shouldUpdateCorrection(sensor)) {
				return true;
			}
		}
	}
	return false;
}
float fromSnapshot(const AirmassLoadSnapshot& s, load_override_e source) {
	if (source > AFR_EffectiveMAP || !(s.ValidSources & (1u << source))) {
		return NAN;
	}
	switch (source) {
		case AFR_None:
			return s.Native;
		case AFR_MAP:
			return s.Map;
		case AFR_Tps:
			return s.Tps;
		case AFR_AccPedal:
			return s.Pedal;
		case AFR_CylFilling:
			return s.Filling;
		case AFR_EffectiveMAP:
			return s.EffectiveMap;
		default:
			return NAN;
	}
}
// Coordinate substitution matches upstream load overrides. The physical validity
// bits remain unchanged, so actuator sensor channels can apply their own fallback.
float consumerFromSnapshot(const AirmassLoadSnapshot& s, load_override_e source) {
	const float value = fromSnapshot(s, source);
	if (std::isfinite(value)) {
		return value;
	}
	switch (source) {
		case AFR_None:
			return s.Native;
		case AFR_MAP:
		case AFR_EffectiveMAP:
			return 200;
		case AFR_Tps:
		case AFR_AccPedal:
			return 100;
		default:
			return NAN;
	}
}
float consumerFromSnapshot(const AirmassLoadSnapshot& s, AirmassConsumer consumer, load_override_e source) {
	if (consumer == AirmassConsumer::HpfpTarget && source == AFR_MAP) {
		// HPFP uses zero on missing measured MAP upstream, independently of
		// the 200 kPa substitute used by lambda and ignition overrides.
		const float value = fromSnapshot(s, source);
		return std::isfinite(value) ? value : 0;
	}
	return consumerFromSnapshot(s, source);
}
void publishCursor(AirmassConsumer consumer, size_t index, float value) {
	// Never cast NaN into the packed representation. Validity remains in the
	// full precision snapshot and injection state, not encoded as a fake load.
	value = std::isfinite(value) ? clampF(-3276, value, 3276) : 0;
	auto& out = engine->outputChannels;
	switch (consumer) {
		case AirmassConsumer::InjectionPhase:
			out.injectionPhaseLoad = value;
			break;
		case AirmassConsumer::FuelTrim:
			out.fuelTrimLoad[index] = value;
			break;
		case AirmassConsumer::IgnitionTrim:
			out.ignitionTrimLoad[index] = value;
			break;
		case AirmassConsumer::Stft:
			out.stftLoad = value;
			break;
		case AirmassConsumer::Staging:
			out.stagingLoad = value;
			break;
		case AirmassConsumer::LambdaDeviation:
			out.lambdaDeviationLoad = value;
			break;
		case AirmassConsumer::LambdaMonitor:
			out.lambdaMonitorLoad = value;
			break;
		case AirmassConsumer::TrailingSpark:
			out.trailingSparkLoad = value;
			break;
		case AirmassConsumer::IgnitionIat:
			out.ignitionIatLoad = value;
			break;
		case AirmassConsumer::KnockRetard:
			out.knockRetardLoad = value;
			break;
		case AirmassConsumer::KnockGain:
			out.knockGainLoad[index] = value;
			break;
		case AirmassConsumer::HpfpTarget:
			out.hpfpTargetLoad = value;
			break;
		default:
			break;
	}
}
} // namespace

bool processAirmassConsumerLoads(const AirmassInputs& inputs, mass_t mass, bool publish, bool* fallbackUsed) {
	AirmassLoadSnapshot snapshot;
	snapshot.Map = inputs.MeasuredMap.value_or(NAN);
	snapshot.EffectiveMap = inputs.EffectiveMap.Valid ? inputs.EffectiveMap.Map : NAN;
	snapshot.Tps = normalizeAirmassPercent(inputs, inputs.Tps).value_or(NAN);
	snapshot.Pedal = normalizeAirmassPercent(inputs, inputs.Pedal).value_or(NAN);
	const auto native = resolveAirmassConsumerLoad(inputs, mass, AFR_None);
	snapshot.Native = native.Value;
	snapshot.Filling = resolveAirmassLoad(inputs, mass, AFR_CylFilling).Value;
	snapshot.UsesEstimate = inputs.EffectiveMap.UsesEstimate;
	snapshot.ConfigurationVersion = inputs.ConfigurationVersion;
	for (uint8_t i = 0; i <= AFR_EffectiveMAP; i++) {
		if (resolveAirmassLoad(inputs, mass, static_cast<load_override_e>(i)).Valid) {
			snapshot.ValidSources |= 1u << i;
		}
	}
	// Optional controllers own their sensor failure policy. A missing coordinate
	// must not turn an otherwise valid air charge into a global fuel cut.
	const bool valid = std::isfinite(mass) && mass >= 0 && resolveAirmassLoad(inputs, mass, AFR_CylFilling).Valid;
	// STFT and protection can read the target even when injection is disabled.
	snapshot.LambdaTargetRequired = lambdaTargetRequired(inputs, snapshot);
	const auto usesFallback = [&](load_override_e source) {
		return !std::isfinite(fromSnapshot(snapshot, source)) && std::isfinite(consumerFromSnapshot(snapshot, source));
	};
	snapshot.RequiredFallbackUsed = (snapshot.LambdaTargetRequired && usesFallback(inputs.LambdaOverride)) ||
									(engineConfiguration->isIgnitionEnabled && usesFallback(inputs.IgnitionOverride));
	for (const auto consumer :
		 {AirmassConsumer::InjectionPhase,
		  AirmassConsumer::FuelTrim,
		  AirmassConsumer::IgnitionTrim,
		  AirmassConsumer::Staging,
		  AirmassConsumer::TrailingSpark,
		  AirmassConsumer::IgnitionIat}) {
		if (!active(consumer, inputs, snapshot)) {
			continue;
		}
		const size_t count = cylinderConsumer(consumer) ? engine->engineState.cylinderCount : 1;
		for (size_t cylinder = 0; cylinder < count && cylinder < MAX_CYLINDER_COUNT; cylinder++) {
			snapshot.RequiredFallbackUsed |= usesFallback(selector(consumer, cylinder));
		}
	}
	if (fallbackUsed) {
		*fallbackUsed = snapshot.RequiredFallbackUsed;
	}
	snapshot.Valid = valid;
	if (publish) {
		chibios_rt::CriticalSectionLocker csl;
		if (!inputs.HasPublicationContext ||
			inputs.PublicationEpoch != engine->airmassInjectionState.publicationEpoch() ||
			inputs.ConfigurationVersion != engine->getGlobalConfigurationVersion() ||
			inputs.ActiveStrategy != engineConfiguration->fuelAlgorithm) {
			return false;
		}
		engine->engineState.airmassLoads = snapshot;
		for (size_t i = 0; i < static_cast<size_t>(AirmassConsumer::Count); i++) {
			const auto consumer = static_cast<AirmassConsumer>(i);
			const size_t count = cylinderConsumer(consumer) ? engine->engineState.cylinderCount : 1;
			for (size_t cylinder = 0; cylinder < count && cylinder < MAX_CYLINDER_COUNT; cylinder++) {
				publishCursor(
						consumer,
						cylinder,
						valid ? consumerFromSnapshot(snapshot, consumer, selector(consumer, cylinder)) : NAN);
			}
		}
	}
	return valid;
}

bool hasAirmassLoadFallback() {
	chibios_rt::CriticalSectionLocker csl;
	const auto& snapshot = engine->engineState.airmassLoads;
	return snapshot.Valid && snapshot.ConfigurationVersion == engine->getGlobalConfigurationVersion() &&
		   snapshot.RequiredFallbackUsed;
}

bool isAirmassLambdaTargetRequired() {
	chibios_rt::CriticalSectionLocker csl;
	const auto& snapshot = engine->engineState.airmassLoads;
	// An invalid publication must never be interpreted as permission to skip validation.
	return !snapshot.Valid || snapshot.ConfigurationVersion != engine->getGlobalConfigurationVersion() ||
		   snapshot.LambdaTargetRequired;
}

float getAirmassSelectedLoad(load_override_e source, float legacyDefault) {
	chibios_rt::CriticalSectionLocker csl;
	const auto& snapshot = engine->engineState.airmassLoads;
	if (snapshot.Valid && snapshot.ConfigurationVersion == engine->getGlobalConfigurationVersion()) {
		return consumerFromSnapshot(snapshot, source);
	}
	if (revisedModel()) {
		return NAN;
	}
	// Lua/mock/legacy external model callers have no capture owner. Explicit
	// physical sources use the same substitute coordinates as upstream overrides.
	switch (source) {
		case AFR_None:
			return legacyDefault;
		case AFR_MAP:
			return Sensor::get(SensorType::Map).value_or(200);
		case AFR_Tps:
			return Sensor::get(SensorType::Tps1).value_or(100);
		case AFR_AccPedal:
			return Sensor::get(SensorType::AcceleratorPedal).value_or(100);
		case AFR_CylFilling:
			return engine->fuelComputer.normalizedCylinderFilling;
		case AFR_EffectiveMAP:
			return Sensor::get(SensorType::Map).value_or(200);
		default:
			return NAN;
	}
}
float getAirmassConsumerLoad(AirmassConsumer consumer, size_t index) {
	chibios_rt::CriticalSectionLocker csl;
	if (index >= MAX_CYLINDER_COUNT) {
		return NAN;
	}
	const auto source = selector(consumer, index);
	const auto& snapshot = engine->engineState.airmassLoads;
	float value;
	if (snapshot.Valid && snapshot.ConfigurationVersion == engine->getGlobalConfigurationVersion()) {
		value = consumerFromSnapshot(snapshot, consumer, source);
	} else if (!revisedModel() && consumer == AirmassConsumer::HpfpTarget && source == AFR_MAP) {
		value = Sensor::getOrZero(SensorType::Map);
	} else {
		value = getAirmassSelectedLoad(source, getFuelingLoad());
	}
	publishCursor(consumer, index, value);
	return value;
}
expected<float> getEffectiveAirmassMap() {
	chibios_rt::CriticalSectionLocker csl;
	const auto& snapshot = engine->engineState.airmassLoads;
	if (!snapshot.Valid || snapshot.ConfigurationVersion != engine->getGlobalConfigurationVersion()) {
		return unexpected;
	}
	const float value = fromSnapshot(snapshot, AFR_EffectiveMAP);
	return std::isfinite(value) ? expected<float>(value) : unexpected;
}
void invalidateAirmassLoads(bool engineStopped) {
	chibios_rt::CriticalSectionLocker csl;
	auto& state = engine->engineState;
	state.airmassLoads.Valid = false;
	state.airmassCalculationValid = false;
	state.injectionDuration = 0;
	state.injectionDurationStage2 = 0;
	state.baseFuel = 0;
	for (auto& cylinder : engine->cylinders) {
		cylinder.setInjectionMass(0);
	}
	state.veAnalyzeSessionInvalid = !engineStopped;
	if (engineStopped) {
		state.veAnalyzeSessionStarted = false;
	}
	state.veAnalyzeEndpoint = 0;
	engine->outputChannels.blendedVeAnalyzeEndpoint = 0;
}
void updateBlendedVeAnalyzeQualification(float rpm) {
	auto& state = engine->engineState;
	if (!(rpm > 0)) {
		// A sensor zero/NaN is not evidence of a physical stop. Only the engine
		// stop transition (or a configuration write at a confirmed stop) can
		// rearm a session after a tune change or airmass fault.
		engine->outputChannels.blendedVeAnalyzeEndpoint = 0;
		return;
	}
	if (!state.veAnalyzeSessionStarted) {
		state.veAnalyzeSessionStarted = true;
		const auto endpoint = config->airmassBlendTable[0][0];
		bool uniform = endpoint == 0 || endpoint == 100;
		for (const auto& row : config->airmassBlendTable) {
			for (auto value : row) {
				uniform &= value == endpoint;
			}
		}
		if (!state.veAnalyzeSessionInvalid && engineConfiguration->fuelAlgorithm == LM_SD_ALPHA_N &&
			!engineConfiguration->useSeparateVeForIdle && uniform) {
			state.veAnalyzeEndpoint = endpoint == 0 ? 1 : 2;
		}
	}
	engine->outputChannels.blendedVeAnalyzeEndpoint =
			state.veAnalyzeSessionInvalid || engine->fuelComputer.running.timeSinceCrankingInSecs < 10
					? 0
					: state.veAnalyzeEndpoint;
}
