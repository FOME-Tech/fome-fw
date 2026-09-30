#pragma once

#include "speed_density_base.h"

enum class AlphaNPressurePolicy {
	PureReference,
	EffectiveMap
};

class AlphaNAirmass : public SpeedDensityBase {
public:
	explicit AlphaNAirmass(const ValueProvider3D* veTable = nullptr)
		: SpeedDensityBase(veTable, LM_ALPHA_N) {}

	AirmassResult getAirmass(float rpm, bool postState) override;
	AirmassEvaluation evaluateAirmass(float rpm, AirmassDiagnostics* diagnostics = nullptr) const;
	AirmassEvaluation
	evaluateAirmass(float rpm, AlphaNPressurePolicy policy, AirmassDiagnostics* diagnostics = nullptr) const;
	AirmassEvaluation getAirmassForFuel(float rpm) const;
	float getVeImpl(float rpm, percent_t load) const override;
	AirmassEvaluation evaluateRawAirmass(
			const AirmassInputs& inputs,
			RawAirmassDiagnostics* diagnostics = nullptr,
			AlphaNPressurePolicy pressurePolicy = AlphaNPressurePolicy::PureReference) const;

private:
	float getDedicatedVeImpl(float rpm, float load) const override;
	AirmassEvaluation evaluateAirmass(float rpm, const DiagnosticsTarget& diagnostics) const;
	AirmassEvaluation
	evaluateAirmass(float rpm, AlphaNPressurePolicy policy, const DiagnosticsTarget& diagnostics) const;
};
