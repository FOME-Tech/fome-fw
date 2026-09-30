#pragma once

#include "airmass.h"

class MafAirmass final : public AirmassVeModelBase {
public:
	explicit MafAirmass(const ValueProvider3D* veTable = nullptr)
		: AirmassVeModelBase(veTable, LM_REAL_MAF) {}

	AirmassResult getAirmass(float rpm, bool postState) override;
	AirmassEvaluation evaluateAirmass(float rpm, AirmassDiagnostics* diagnostics = nullptr) const;
	AirmassEvaluation getAirmassForFuel(float rpm) const;
	float getVeImpl(float rpm, percent_t load) const override;

	// Compute airmass based on flow & engine speed
	AirmassResult getAirmassImpl(float massAirFlow, float rpm, bool postState) const;
	AirmassEvaluation
	evaluateAirmassImpl(float massAirFlow, float rpm, AirmassDiagnostics* diagnostics = nullptr) const;

private:
	AirmassEvaluation evaluateAirmass(float rpm, const DiagnosticsTarget& diagnostics) const;
	AirmassEvaluation evaluateAirmassImpl(float massAirFlow, float rpm, const DiagnosticsTarget& diagnostics) const;
	float getMaf(bool& valid) const;
};
