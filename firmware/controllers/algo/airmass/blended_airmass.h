#pragma once

#include "airmass.h"
#include "airmass_injection_state.h"

class SpeedDensityAirmass;
class AlphaNAirmass;

enum BlendedAirmassFlags : uint16_t {
	BlendedSdEvaluated = 1 << 0,
	BlendedAlphaNEvaluated = 1 << 1,
	BlendedSdValid = 1 << 2,
	BlendedAlphaNValid = 1 << 3,
	BlendedMapEstimateUsed = 1 << 4,
	BlendedCalculationValid = 1 << 5,
	BlendedEstimateEvaluated = 1 << 6,
	BlendedTemperatureFallback = 1 << 7,
	BlendedBranchFallback = 1 << 8,
	BlendedAuthorityUnavailable = 1 << 9,
	BlendedBaroFallback = 1 << 10,
	BlendedIdleFallback = 1 << 11,
	BlendedCorrectionFallback = 1 << 12,
	BlendedMapFallback = 1 << 13,
	BlendedLoadFallback = 1 << 14,
};

struct BlendedAirmassEvaluation {
	bool Degraded = false;
	AirmassEvaluation Airmass;
	AirmassLoad LambdaLoad;
	AirmassLoad IgnitionLoad;
	float NormalizedFilling = 0;
	AirmassInjectionFault Fault = AirmassInjectionFault::None;
};

// Optional capture. The live path publishes through a small delivery target and
// does not reserve this complete diagnostics buffer on the main-loop stack.
struct BlendedAirmassDiagnostics {
	float TemperatureK = 0;
	bool TemperatureValid = false;
	bool TemperatureFallback = false;
	AirmassTemperatureSource TemperatureSource = AirmassTemperatureSource::Tcharge;
	float BaroCoefficient = 1;
	uint8_t PressureFlags = 0;
	float SdMass = 0;
	float AlphaNMass = 0;
	RawAirmassDiagnostics Sd;
	RawAirmassDiagnostics AlphaN;
	MapEvaluation Map;
	float RequestedAuthority = 0;
	float EffectiveAuthority = 0;
	VeCorrectionDiagnostics Corrections;
	uint16_t Flags = 0;
};

bool isBlendedAirmassConfigurationValid();

class BlendedAirmass final : public AirmassModelBase {
public:
	BlendedAirmass(const SpeedDensityAirmass& sd, const AlphaNAirmass& alphaN)
		: m_sd(sd)
		, m_alphaN(alphaN) {}

	AirmassResult getAirmass(float rpm, bool postState) override;
	BlendedAirmassEvaluation evaluateAirmass(float rpm, BlendedAirmassDiagnostics* diagnostics = nullptr) const;
	// Publishes diagnostics only. The fuel owner owns injection-fault acceptance
	// and rejection; engine2 completes readiness after per-cylinder publication.
	BlendedAirmassEvaluation getAirmassForFuel(float rpm) const;

private:
	class DiagnosticsTarget;
	BlendedAirmassEvaluation evaluateAirmass(float rpm, DiagnosticsTarget& diagnostics) const;
	const SpeedDensityAirmass& m_sd;
	const AlphaNAirmass& m_alphaN;
};
