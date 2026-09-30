#pragma once

#include "speed_density_base.h"

class SpeedDensityAirmass : public SpeedDensityBase {
public:
	explicit SpeedDensityAirmass(const ValueProvider3D* veTable, const ValueProvider3D& mapEstimationTable)
		: SpeedDensityBase(veTable)
		, m_mapEstimationTable(&mapEstimationTable) {}

	AirmassResult getAirmass(float rpm, bool postState) override;
	AirmassResult getAirmass(float rpm, float map, bool postState);
	AirmassEvaluation evaluateAirmass(float rpm, AirmassDiagnostics* diagnostics = nullptr) const;
	AirmassEvaluation evaluateAirmass(float rpm, float map, AirmassDiagnostics* diagnostics = nullptr) const;
	AirmassEvaluation getAirmassForFuel(float rpm) const;
	float getVeImpl(float rpm, percent_t load) const override;
	float getAirflow(float rpm, float map, bool postState);

	float getMap(float rpm, bool postState) const;
	MapEvaluation evaluateMap(float rpm) const;
	void captureInputs(float rpm, AirmassInputs& inputs) const;
	AirmassEvaluation
	evaluateRawAirmass(const AirmassInputs& inputs, RawAirmassDiagnostics* diagnostics = nullptr) const;

private:
	AirmassEvaluation evaluateAirmass(AirmassInputs& inputs, const DiagnosticsTarget& diagnostics) const;
	AirmassEvaluation evaluateAirmass(float rpm, const DiagnosticsTarget& diagnostics) const;
	AirmassEvaluation evaluateAirmass(float rpm, float map, const DiagnosticsTarget& diagnostics) const;
	const ValueProvider3D* const m_mapEstimationTable;
};
