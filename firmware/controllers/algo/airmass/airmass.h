#pragma once

#include "rusefi_types.h"
struct blend_table_s;
#include "engine_math.h"
#include <rusefi/expected.h>

class ValueProvider3D;

// Queries must not change engine fault state. The live fuel owner and config
// application use the fatal validator; model evaluations use the pure predicate.
bool isAirmassConfigurationValid();
bool validateAirmassConfiguration();

struct AirmassResult {
	mass_t CylinderAirmass = 0;
	percent_t EngineLoadPercent = 100;
};

struct AirmassModelBase {
	virtual AirmassResult getAirmass(float rpm, bool postState) = 0;
};

struct VeEvaluation {
	percent_t Ve = 0;
	// Required table, idle and correction inputs are usable.
	bool Valid = false;
	bool Fallback = false;
};

// Optional caller-owned diagnostics contain values, never references to live state.
// On reuse, absent payloads are retained but HasValue and Valid are cleared.
struct VeDiagnostics {
	// Final VE in percent, after idle interpolation and correction multipliers.
	percent_t Ve = 0;
	float Load = 0;
	float IdleLoad = 0;
	BlendResult Blends[VE_BLEND_COUNT] = {};
	// Diagnostics were calculated and may be published even when Valid is false.
	bool HasValue = false;
	// Includes validity of every enabled correction input.
	bool Valid = false;
};

struct MapEvaluation {
	float Map = 0;
	float FallbackMap = 0;
	bool HasValue = false; // A fallback MAP diagnostic was evaluated for publication.
	bool Valid = false;
	bool UsesEstimate = false;
	bool Fallback = false;
};

// Shared inputs for a physical-model calculation. Sensor acquisition is sequential,
// not atomic. Maps remain in configuration storage and must not be copied here.
// Optional inputs retain their own validity: an unused input cannot fail a model.
struct AirmassInputs {
	// Captured together before sensor acquisition. A stop, tune write, or new
	// calculation must prevent this capture from publishing consumer state.
	uint32_t PublicationEpoch = 0;
	int ConfigurationVersion = 0;
	engine_load_mode_e ActiveStrategy = LM_SPEED_DENSITY;
	bool HasPublicationContext = false;
	float Rpm = 0;
	expected<float> MeasuredMap = unexpected;
	expected<float> Tps = unexpected;
	expected<float> Pedal = unexpected;
	// Captured sensor error limits; default values match the standard sensor
	// configuration for callers that construct a snapshot directly.
	float TpsToleranceMin = -10;
	float TpsToleranceMax = 110;
	expected<float> Iat = unexpected;
	MapEvaluation EffectiveMap;
	float TemperatureK = 0;
	bool TemperatureValid = false;
	bool TemperatureFallback = false;
	AirmassTemperatureSource TemperatureSource = AirmassTemperatureSource::Tcharge;
	expected<float> BarometricPressure = unexpected;
	expected<float> DriverThrottleIntent = unexpected;
	bool BaroFromStartup = false;
	bool IdleActive = false;
	bool Composite = false;
	engine_load_mode_e Model = LM_SPEED_DENSITY;
	float NativeLoad = 0;
	float Displacement = 0;
	float CylinderCount = 0;
	float PreviousFuelingLoad = 0;
	float PreviousIgnitionLoad = 0;
	load_override_e LambdaOverride = AFR_None;
	load_override_e IgnitionOverride = AFR_None;
};

// Convert a captured TPS-like sensor value into a table coordinate. The sensor
// must be valid and within its configured error-detection bounds; tolerated
// overtravel is then represented by the nearest 0..100% coordinate.
expected<float> normalizeAirmassPercent(const AirmassInputs& inputs, expected<float> sensor);

struct RawAirmassDiagnostics {
	bool IdleFallback = false;
	bool BaroFallback = false;
	float TableValue = 0;
	float IdleLoad = 0;
	float IdleWeight = 0;
	float BaroCoefficient = 1;
	bool HasValue = false;
	bool Valid = false;
};

enum class AirmassLoadSource : uint8_t {
	Invalid,
	EffectiveMap,
	MeasuredMap,
	Tps,
	Pedal,
	CylinderFilling
};
enum class AirmassLoadUnit : uint8_t {
	Kpa,
	Percent
};

struct AirmassLoad {
	float Value = 0;
	AirmassLoadSource Source = AirmassLoadSource::Invalid;
	AirmassLoadUnit Unit = AirmassLoadUnit::Percent;
	bool Valid = false;
	bool UsesEstimate = false;
};

// Pure resolution: never re-read sensors or apply legacy numeric failure fallbacks.
AirmassLoad resolveAirmassLoad(const AirmassInputs& inputs, mass_t finalMass, load_override_e selector);
// Upstream numeric substitutes for downstream tables; physical resolution stays strict.
AirmassLoad resolveAirmassConsumerLoad(const AirmassInputs& inputs, mass_t finalMass, load_override_e selector);

struct VeCorrectionEvaluation {
	float Multiplier = 1;
	bool Valid = false;
	bool Fallback = false;
};

struct VeCorrectionDiagnostics {
	bool Fallback = false;
	BlendResult Blends[VE_BLEND_COUNT] = {};
	bool HasValue = false;
	bool Valid = false;
};

// Call once after input capture, before publishing any new load. Core inputs and
// previous loads come from the capture; other configured channels are sampled once
// per distinct channel during this pass. No idle overlay or downstream correction.
VeCorrectionEvaluation
evaluateAirmassCorrections(const AirmassInputs& inputs, VeCorrectionDiagnostics* diagnostics = nullptr);
// Live owner delivery without allocating an optional diagnostics array.
VeCorrectionEvaluation evaluateAirmassCorrectionsForFuel(const AirmassInputs& inputs);

bool isMapEstimateConfigurationValid();
bool isMapEstimateAxesValid();
bool isRawAirmassConfigurationValid();
bool isAirmassModelConfigurationValid(engine_load_mode_e model);
void captureAirmassInputs(
		float rpm, AirmassInputs& inputs, const ValueProvider3D* estimate = nullptr, bool resolveMap = true);
void resolveCapturedMap(AirmassInputs& inputs, const ValueProvider3D* estimate = nullptr);
// Implemented by the live load consumer owner. Dry queries validate without publication.
bool processAirmassConsumerLoads(const AirmassInputs& inputs, mass_t mass, bool publish, bool* fallbackUsed = nullptr);

struct AirmassEvaluation {
	bool Degraded = false;
	AirmassResult Result;
	// Describes usable inputs/results separately from legacy numeric fault fallbacks.
	// Table compatibility is checked; calibration quality and composite fault
	// policy are not evaluated here.
	bool Valid = false;
};

// Pressure flags: 1 continuous BARO, 2 startup BARO, 4 invalid BARO,
// 8 standalone MAP multiplication. Coefficient is 1 when BARO is inapplicable.
uint8_t getAirmassPressureFlags(const AirmassInputs& inputs, bool multiplyMap);
void publishAirmassTemperature(const AirmassInputs& inputs);
void publishAirmassPressure(const AirmassInputs& inputs, float coefficient, bool multiplyMap);

struct AirmassDiagnostics {
	float TemperatureK = 0;
	bool TemperatureValid = false;
	bool TemperatureFallback = false;
	AirmassTemperatureSource TemperatureSource = AirmassTemperatureSource::Tcharge;
	float BaroCoefficient = 1;
	uint8_t PressureFlags = 0;
	VeDiagnostics Ve;
	MapEvaluation Map;
};

class AirmassVeModelBase : public AirmassModelBase {
public:
	explicit AirmassVeModelBase(const ValueProvider3D* veTable, engine_load_mode_e model = LM_SPEED_DENSITY);

	// Retrieve the user-calibrated volumetric efficiency from the table
	float getVe(float rpm, percent_t load, bool postState) const;
	// Evaluation does not publish live diagnostics. Legacy warnings remain enabled.
	VeEvaluation evaluateVe(float rpm, percent_t load, VeDiagnostics* diagnostics = nullptr) const;
	static void publishVe(const VeDiagnostics& diagnostics);
	static void publishEvaluation(const AirmassDiagnostics& diagnostics);

	virtual float getVeImpl(float /*rpm*/, percent_t /*load*/) const;

protected:
	class DiagnosticsTarget;
	VeEvaluation evaluateRawVe(const AirmassInputs& inputs, float load, RawAirmassDiagnostics* diagnostics) const;
	VeEvaluation evaluateVe(const AirmassInputs& inputs, float load, const DiagnosticsTarget& diagnostics) const;
	virtual float getDedicatedVeImpl(float rpm, float load) const;

	// Legacy wrappers select live delivery. Public evaluations only select optional
	// capture, so neither dry nor live calculations need a diagnostics array local.
	class DiagnosticsTarget {
	public:
		explicit DiagnosticsTarget(bool postState);
		explicit DiagnosticsTarget(VeDiagnostics* diagnostics);
		explicit DiagnosticsTarget(AirmassDiagnostics* diagnostics);

		void blend(size_t index, const BlendResult& result) const;
		void ve(const VeEvaluation& result, float load, float idleLoad) const;
		void map(const MapEvaluation& result) const;
		void temperature(const AirmassInputs& inputs) const;
		void pressure(const AirmassInputs& inputs, float coefficient, bool multiplyMap) const;
		bool consumers(const AirmassInputs& inputs, mass_t mass) const;
		VeCorrectionEvaluation corrections(const AirmassInputs& inputs) const;

	private:
		AirmassDiagnostics* m_airmass = nullptr;
		VeDiagnostics* m_ve = nullptr;
		MapEvaluation* m_map = nullptr;
		bool m_postState = false;
	};

	VeEvaluation evaluateVe(float rpm, percent_t load, const DiagnosticsTarget& diagnostics) const;

private:
	const ValueProvider3D* const m_veTable;
	const engine_load_mode_e m_model;
};
