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
// Diagnostics follow feature enablement, not dependency qualification. Public
// coordinate getters remain usable regardless of these flags. Capture once per
// publication, including the restore coordinate of a disabled, latched monitor.
using ConsumerMask = uint16_t;
static_assert(static_cast<unsigned>(AirmassConsumer::Count) <= 16);
constexpr ConsumerMask consumerBit(AirmassConsumer consumer) {
	return ConsumerMask{1} << static_cast<unsigned>(consumer);
}
ConsumerMask enabledConsumerMask(float rpm) {
	ConsumerMask result = 0;
	const auto include = [&](AirmassConsumer consumer, bool enabled) {
		if (enabled) {
			result |= consumerBit(consumer);
		}
	};
	include(AirmassConsumer::InjectionPhase, engineConfiguration->isInjectionEnabled);
	include(AirmassConsumer::FuelTrim, engineConfiguration->isInjectionEnabled);
	include(AirmassConsumer::IgnitionTrim, engineConfiguration->isIgnitionEnabled);
	include(AirmassConsumer::Stft, engineConfiguration->fuelClosedLoopCorrectionEnabled);
	include(AirmassConsumer::Staging,
			engineConfiguration->isInjectionEnabled && engineConfiguration->enableStagedInjection);
	include(AirmassConsumer::LambdaDeviation, engineConfiguration->lambdaProtectionEnable);
	include(AirmassConsumer::LambdaMonitor,
			engineConfiguration->lambdaProtectionEnable || engine->lambdaMonitor.isCut());
	include(AirmassConsumer::TrailingSpark,
			engineConfiguration->isIgnitionEnabled && engineConfiguration->enableTrailingSparks);
	include(AirmassConsumer::IgnitionIat, engineConfiguration->isIgnitionEnabled);
	include(AirmassConsumer::KnockRetard, engineConfiguration->enableSoftwareKnock);
	include(AirmassConsumer::KnockGain, engineConfiguration->enableSoftwareKnock);
	include(AirmassConsumer::HpfpTarget,
			rpm >= 60 && enginePins.hpfpValve.isInitialized() && engineConfiguration->hpfpCamLobes &&
					engineConfiguration->hpfpPumpVolume > 0);
	return result;
}
#if EFI_UNIT_TEST
uint32_t cursorPreparationCounts[static_cast<unsigned>(AirmassConsumer::Count)]{};
uint32_t consumerReadCounts[static_cast<unsigned>(AirmassConsumer::Count)]{};
#endif
// All diagnostic load fields use the same scaled type. Keep the actual packed
// type here so publication is a copy, without another rounding conversion.
using PackedLoad = decltype(output_channels_s::injectionPhaseLoad);
template <typename T>
struct ConsumerCursors {
	T injectionPhaseLoad{};
	T fuelTrimLoad[MAX_CYLINDER_COUNT]{};
	T ignitionTrimLoad[MAX_CYLINDER_COUNT]{};
	T stftLoad{};
	T stagingLoad{};
	T lambdaDeviationLoad{};
	T lambdaMonitorLoad{};
	T trailingSparkLoad{};
	T ignitionIatLoad{};
	T knockRetardLoad{};
	T knockGainLoad[MAX_CYLINDER_COUNT]{};
	T hpfpTargetLoad{};
};
using PackedConsumerCursors = ConsumerCursors<PackedLoad>;
static_assert(sizeof(PackedConsumerCursors) == 90);

template <typename T, typename Reader>
void prepareConsumerCursors(
		ConsumerCursors<T>& result, const Reader& read, size_t cylinderCount, ConsumerMask enabled) {
	const auto readEnabled = [&](AirmassConsumer consumer, size_t index) {
		return enabled & consumerBit(consumer) ? read(consumer, index) : T{};
	};
	result.injectionPhaseLoad = readEnabled(AirmassConsumer::InjectionPhase, 0);
	result.stftLoad = readEnabled(AirmassConsumer::Stft, 0);
	result.stagingLoad = readEnabled(AirmassConsumer::Staging, 0);
	result.lambdaDeviationLoad = readEnabled(AirmassConsumer::LambdaDeviation, 0);
	result.lambdaMonitorLoad = readEnabled(AirmassConsumer::LambdaMonitor, 0);
	result.trailingSparkLoad = readEnabled(AirmassConsumer::TrailingSpark, 0);
	result.ignitionIatLoad = readEnabled(AirmassConsumer::IgnitionIat, 0);
	result.knockRetardLoad = readEnabled(AirmassConsumer::KnockRetard, 0);
	result.hpfpTargetLoad = readEnabled(AirmassConsumer::HpfpTarget, 0);
	for (size_t i = 0; i < cylinderCount && i < MAX_CYLINDER_COUNT; i++) {
		result.fuelTrimLoad[i] = readEnabled(AirmassConsumer::FuelTrim, i);
		result.ignitionTrimLoad[i] = readEnabled(AirmassConsumer::IgnitionTrim, i);
		result.knockGainLoad[i] = readEnabled(AirmassConsumer::KnockGain, i);
	}
}
PackedLoad packCursor(float value, AirmassConsumer consumer) {
#if EFI_UNIT_TEST
	cursorPreparationCounts[static_cast<unsigned>(consumer)]++;
#else
	UNUSED(consumer);
#endif
	// Never cast NaN into the packed representation. Validity remains in the
	// full precision snapshot and injection state, not encoded as a fake load.
	return std::isfinite(value) ? clampF(-3276, value, 3276) : 0;
}
void publishConsumerCursors(const PackedConsumerCursors& cursors) {
	auto& out = engine->outputChannels;
	// Compile-time checks prevent a changed output scale from silently adding
	// numeric conversion back into this interrupt-masked copy.
#define COPY_CURSOR(field)                                                                                             \
	static_assert(std::is_same_v<decltype(out.field), decltype(cursors.field)>);                                       \
	out.field = cursors.field
	COPY_CURSOR(injectionPhaseLoad);
	COPY_CURSOR(stftLoad);
	COPY_CURSOR(stagingLoad);
	COPY_CURSOR(lambdaDeviationLoad);
	COPY_CURSOR(lambdaMonitorLoad);
	COPY_CURSOR(trailingSparkLoad);
	COPY_CURSOR(ignitionIatLoad);
	COPY_CURSOR(knockRetardLoad);
	COPY_CURSOR(hpfpTargetLoad);
#undef COPY_CURSOR
	static_assert(std::is_same_v<decltype(out.fuelTrimLoad), decltype(cursors.fuelTrimLoad)>);
	static_assert(std::is_same_v<decltype(out.ignitionTrimLoad), decltype(cursors.ignitionTrimLoad)>);
	static_assert(std::is_same_v<decltype(out.knockGainLoad), decltype(cursors.knockGainLoad)>);
	std::copy_n(cursors.fuelTrimLoad, MAX_CYLINDER_COUNT, out.fuelTrimLoad);
	std::copy_n(cursors.ignitionTrimLoad, MAX_CYLINDER_COUNT, out.ignitionTrimLoad);
	std::copy_n(cursors.knockGainLoad, MAX_CYLINDER_COUNT, out.knockGainLoad);
}
} // namespace

bool processAirmassConsumerLoads(const AirmassInputs& inputs, mass_t mass, bool publish, bool* fallbackUsed) {
	const AirmassResolvedLoads loads(inputs, mass);
	return processAirmassConsumerLoads(inputs, mass, loads, publish, fallbackUsed);
}

bool processAirmassConsumerLoads(
		const AirmassInputs& inputs, mass_t mass, const AirmassResolvedLoads& loads, bool publish, bool* fallbackUsed) {
	AirmassLoadSnapshot snapshot;
	snapshot.Map = inputs.MeasuredMap.value_or(NAN);
	snapshot.EffectiveMap = inputs.EffectiveMap.Valid ? loads.strict(AFR_EffectiveMAP).Value : NAN;
	snapshot.Tps = loads.strict(AFR_Tps).Valid ? loads.strict(AFR_Tps).Value : NAN;
	snapshot.Pedal = loads.strict(AFR_AccPedal).Valid ? loads.strict(AFR_AccPedal).Value : NAN;
	snapshot.Native = loads.consumer(AFR_None).Value;
	snapshot.Filling = loads.strict(AFR_CylFilling).Value;
	snapshot.UsesEstimate = inputs.EffectiveMap.UsesEstimate;
	snapshot.ConfigurationVersion = inputs.ConfigurationVersion;
	for (uint8_t i = 0; i <= AFR_EffectiveMAP; i++) {
		if (loads.strict(static_cast<load_override_e>(i)).Valid) {
			snapshot.ValidSources |= 1u << i;
		}
	}
	// Optional controllers own their sensor failure policy. A missing coordinate
	// must not turn an otherwise valid air charge into a global fuel cut.
	const bool valid = std::isfinite(mass) && mass >= 0 && loads.strict(AFR_CylFilling).Valid;
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
		PackedConsumerCursors cursors;
		prepareConsumerCursors(
				cursors,
				[&](AirmassConsumer consumer, size_t index) {
					return packCursor(
							valid ? consumerFromSnapshot(snapshot, consumer, selector(consumer, index)) : NAN,
							consumer);
				},
				engine->engineState.cylinderCount,
				enabledConsumerMask(inputs.Rpm));
#if EFI_UNIT_TEST
		if (engine->onAirmassConsumerLoadsPrepared) {
			engine->onAirmassConsumerLoadsPrepared();
		}
#endif
		chibios_rt::CriticalSectionLocker csl;
		if (!inputs.HasPublicationContext ||
			inputs.PublicationEpoch != engine->airmassInjectionState.locked(csl).publicationEpoch() ||
			inputs.CalibrationGeneration != engine->airmassCalibration.Generation ||
			inputs.ConfigurationVersion != engine->getGlobalConfigurationVersion() ||
			inputs.ActiveStrategy != engineConfiguration->fuelAlgorithm) {
			return false;
		}
		engine->engineState.airmassLoads = snapshot;
		publishConsumerCursors(cursors);
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

namespace {
float readSelectedLoad(load_override_e source, float legacyDefault) {
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
float readConsumerLoad(AirmassConsumer consumer, size_t index) {
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
		value = readSelectedLoad(source, getFuelingLoad());
	}
	return value;
}
void captureConsumerCoordinates(
		float (&loads)[AFR_EffectiveMAP + 1], float& hpfpMap, uint8_t sources = (1u << (AFR_EffectiveMAP + 1)) - 1) {
	const auto& snapshot = engine->engineState.airmassLoads;
	if (snapshot.Valid && snapshot.ConfigurationVersion == engine->getGlobalConfigurationVersion()) {
		for (size_t i = 0; i <= AFR_EffectiveMAP; i++) {
			loads[i] = sources & (1u << i) ? consumerFromSnapshot(snapshot, static_cast<load_override_e>(i)) : NAN;
		}
		hpfpMap =
				sources & (1u << AFR_MAP) ? consumerFromSnapshot(snapshot, AirmassConsumer::HpfpTarget, AFR_MAP) : NAN;
		return;
	}
	if (revisedModel()) {
		for (auto& load : loads) {
			load = NAN;
		}
		hpfpMap = NAN;
		return;
	}
	// Sample each physical source once for external-model cylinder loops too.
	const auto map = sources & ((1u << AFR_MAP) | (1u << AFR_EffectiveMAP)) ? Sensor::get(SensorType::Map) : unexpected;
	loads[AFR_None] = sources & (1u << AFR_None) ? getFuelingLoad() : NAN;
	loads[AFR_MAP] = loads[AFR_EffectiveMAP] = map.value_or(200);
	loads[AFR_Tps] = sources & (1u << AFR_Tps) ? Sensor::get(SensorType::Tps1).value_or(100) : NAN;
	loads[AFR_AccPedal] =
			sources & (1u << AFR_AccPedal) ? Sensor::get(SensorType::AcceleratorPedal).value_or(100) : NAN;
	loads[AFR_CylFilling] = sources & (1u << AFR_CylFilling) ? engine->fuelComputer.normalizedCylinderFilling : NAN;
	hpfpMap = map.value_or(0);
}
float coordinate(
		const float (&loads)[AFR_EffectiveMAP + 1], float hpfpMap, AirmassConsumer consumer, load_override_e source) {
	if (source > AFR_EffectiveMAP) {
		return NAN;
	}
	return consumer == AirmassConsumer::HpfpTarget && source == AFR_MAP ? hpfpMap : loads[source];
}
} // namespace

float getAirmassSelectedLoad(load_override_e source, float legacyDefault) {
	chibios_rt::CriticalSectionLocker csl;
	return readSelectedLoad(source, legacyDefault);
}
float getAirmassConsumerLoad(AirmassConsumer consumer, size_t index) {
#if EFI_UNIT_TEST
	if (consumer < AirmassConsumer::Count) {
		consumerReadCounts[static_cast<unsigned>(consumer)]++;
	}
#endif
	chibios_rt::CriticalSectionLocker csl;
	return readConsumerLoad(consumer, index);
}
AirmassConsumerLoadContext::AirmassConsumerLoadContext(AirmassConsumer first, AirmassConsumer second)
	: m_first(first)
	, m_second(second) {
	if (first == AirmassConsumer::Count && second == AirmassConsumer::Count) {
		return;
	}
	chibios_rt::CriticalSectionLocker csl;
	captureConsumerCoordinates(m_loads, m_hpfpMap);
	for (size_t i = 0; i < MAX_CYLINDER_COUNT; i++) {
		if (first != AirmassConsumer::Count) {
			m_sources[0][i] = selector(first, i);
		}
		if (second != AirmassConsumer::Count) {
			m_sources[1][i] = selector(second, i);
		}
	}
}
float AirmassConsumerLoadContext::get(AirmassConsumer consumer, size_t index) const {
	if (index >= MAX_CYLINDER_COUNT || consumer == AirmassConsumer::Count) {
		return NAN;
	}
	const size_t group = consumer == m_first ? 0 : 1;
	if (group == 1 && consumer != m_second) {
		return NAN;
	}
#if EFI_UNIT_TEST
	consumerReadCounts[static_cast<unsigned>(consumer)]++;
#endif
	return coordinate(m_loads, m_hpfpMap, consumer, m_sources[group][index]);
}
void publishLegacyAirmassConsumerLoads() {
	if (revisedModel()) {
		return;
	}
	float loads[AFR_EffectiveMAP + 1];
	float hpfpMap;
	ConsumerCursors<load_override_e> sources;
	AirmassInjectionState::CalculationToken epoch;
	int configurationVersion;
	engine_load_mode_e strategy;
	size_t cylinderCount;
	ConsumerMask enabled;
	{
		chibios_rt::CriticalSectionLocker csl;
		if (revisedModel()) {
			return;
		}
		epoch = engine->airmassInjectionState.locked(csl).publicationEpoch();
		configurationVersion = engine->getGlobalConfigurationVersion();
		strategy = engineConfiguration->fuelAlgorithm;
		cylinderCount = engine->engineState.cylinderCount;
		enabled = enabledConsumerMask(Sensor::getOrZero(SensorType::Rpm));
		uint8_t requiredSources = 0;
		prepareConsumerCursors(
				sources,
				[&](AirmassConsumer consumer, size_t index) {
					const auto source = selector(consumer, index);
					if (source <= AFR_EffectiveMAP) {
						requiredSources |= 1u << source;
					}
					return source;
				},
				cylinderCount,
				enabled);
		captureConsumerCoordinates(loads, hpfpMap, requiredSources);
	}
	PackedConsumerCursors cursors;
	prepareConsumerCursors(
			cursors,
			[&](AirmassConsumer consumer, size_t index) {
				// Selector resolution happens outside the publication lock too.
				load_override_e source;
				switch (consumer) {
					case AirmassConsumer::InjectionPhase:
						source = sources.injectionPhaseLoad;
						break;
					case AirmassConsumer::FuelTrim:
						source = sources.fuelTrimLoad[index];
						break;
					case AirmassConsumer::IgnitionTrim:
						source = sources.ignitionTrimLoad[index];
						break;
					case AirmassConsumer::Stft:
						source = sources.stftLoad;
						break;
					case AirmassConsumer::Staging:
						source = sources.stagingLoad;
						break;
					case AirmassConsumer::LambdaDeviation:
						source = sources.lambdaDeviationLoad;
						break;
					case AirmassConsumer::LambdaMonitor:
						source = sources.lambdaMonitorLoad;
						break;
					case AirmassConsumer::TrailingSpark:
						source = sources.trailingSparkLoad;
						break;
					case AirmassConsumer::IgnitionIat:
						source = sources.ignitionIatLoad;
						break;
					case AirmassConsumer::KnockRetard:
						source = sources.knockRetardLoad;
						break;
					case AirmassConsumer::KnockGain:
						source = sources.knockGainLoad[index];
						break;
					case AirmassConsumer::HpfpTarget:
						source = sources.hpfpTargetLoad;
						break;
					default:
						source = static_cast<load_override_e>(255);
						break;
				}
				return packCursor(coordinate(loads, hpfpMap, consumer, source), consumer);
			},
			cylinderCount,
			enabled);
#if EFI_UNIT_TEST
	if (engine->onAirmassConsumerLoadsPrepared) {
		engine->onAirmassConsumerLoadsPrepared();
	}
#endif
	chibios_rt::CriticalSectionLocker csl;
	if (epoch != engine->airmassInjectionState.locked(csl).publicationEpoch() ||
		configurationVersion != engine->getGlobalConfigurationVersion() ||
		strategy != engineConfiguration->fuelAlgorithm) {
		return;
	}
	publishConsumerCursors(cursors);
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
void invalidateAirmassConsumerLoads() {
	chibios_rt::CriticalSectionLocker csl;
	engine->engineState.airmassLoads.Valid = false;
	// Clear unused cylinder slots too, including after a cylinder-count write.
	publishConsumerCursors(PackedConsumerCursors{});
}
void invalidateAirmassLoads(bool engineStopped) {
	chibios_rt::CriticalSectionLocker csl;
	auto& state = engine->engineState;
	invalidateAirmassConsumerLoads();
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

#if EFI_UNIT_TEST
void resetAirmassCursorPreparationCounts() {
	std::fill_n(cursorPreparationCounts, static_cast<unsigned>(AirmassConsumer::Count), 0);
	std::fill_n(consumerReadCounts, static_cast<unsigned>(AirmassConsumer::Count), 0);
}
uint32_t getAirmassCursorPreparationCount(AirmassConsumer consumer) {
	return cursorPreparationCounts[static_cast<unsigned>(consumer)];
}
uint32_t getAirmassConsumerReadCount(AirmassConsumer consumer) {
	return consumerReadCounts[static_cast<unsigned>(consumer)];
}
#endif
