#include "pch.h"

#include "maf_airmass.h"
#include "maf.h"
#include "fuel_math.h"

float MafAirmass::getMaf(bool& valid) const {
	auto maf = Sensor::get(SensorType::Maf);
	if (maf && (!std::isfinite(maf.Value) || maf.Value < 0)) {
		maf = unexpected;
	}
	valid = maf.Valid;

	if (Sensor::hasSensor(SensorType::Maf2)) {
		auto maf2 = Sensor::get(SensorType::Maf2);
		if (maf2 && (!std::isfinite(maf2.Value) || maf2.Value < 0)) {
			maf2 = unexpected;
		}
		valid = maf.Valid || maf2.Valid;

		if (maf && maf2) {
			// Both MAFs work, return the sum
			return maf.Value + maf2.Value;
		} else if (maf) {
			// MAF 1 works, but not MAF 2, so double the value from #1
			return 2 * maf.Value;
		} else if (maf2) {
			// MAF 2 works, but not MAF 1, so double the value from #2
			return 2 * maf2.Value;
		} else {
			// Both MAFs are broken, give up.
			return 0;
		}
	} else {
		return maf.value_or(0);
	}
}

AirmassResult MafAirmass::getAirmass(float rpm, bool postState) {
	return evaluateAirmass(rpm, DiagnosticsTarget(postState)).Result;
}

AirmassEvaluation MafAirmass::getAirmassForFuel(float rpm) const {
	return evaluateAirmass(rpm, DiagnosticsTarget(true));
}

AirmassEvaluation MafAirmass::evaluateAirmass(float rpm, AirmassDiagnostics* diagnostics) const {
	return evaluateAirmass(rpm, DiagnosticsTarget(diagnostics));
}

AirmassEvaluation MafAirmass::evaluateAirmass(float rpm, const DiagnosticsTarget& diagnostics) const {
	bool valid;
	float maf = getMaf(valid);
	if (!valid) {
		return {};
	}
	auto evaluation = evaluateAirmassImpl(maf, rpm, diagnostics);
	evaluation.Valid = evaluation.Valid && valid;
	return evaluation;
}

/**
 * Function block now works to create a standardised load from the cylinder filling as well as tune fuel via VE table.
 * @return total duration of fuel injection per engine cycle, in milliseconds
 */
AirmassResult MafAirmass::getAirmassImpl(float massAirFlow, float rpm, bool postState) const {
	return evaluateAirmassImpl(massAirFlow, rpm, DiagnosticsTarget(postState)).Result;
}

AirmassEvaluation MafAirmass::evaluateAirmassImpl(float massAirFlow, float rpm, AirmassDiagnostics* diagnostics) const {
	return evaluateAirmassImpl(massAirFlow, rpm, DiagnosticsTarget(diagnostics));
}

AirmassEvaluation
MafAirmass::evaluateAirmassImpl(float massAirFlow, float rpm, const DiagnosticsTarget& diagnostics) const {
	AirmassEvaluation evaluation;
	// Validate before converting flow or interpolating a table. A stopped engine
	// has no meaningful mass-per-cycle conversion.
	if (!std::isfinite(rpm) || rpm <= 0 || !std::isfinite(massAirFlow) || massAirFlow < 0 ||
		!isRawAirmassConfigurationValid()) {
		return evaluation;
	}

	// kg/hr -> g/s
	float gramPerSecond = massAirFlow / 3.6f;

	// 1/min -> 1/s
	float revsPerSecond = rpm / 60.0f;
	mass_t airPerRevolution = gramPerSecond / revsPerSecond;

	// Now we have to divide among cylinders - on a 4 stroke, half of the cylinders happen every revolution
	// This math is floating point to work properly on engines with odd cylinder count
	float halfCylCount = engine->engineState.cylinderCount / 2.0f;

	mass_t cylinderAirmass = airPerRevolution / halfCylCount;

	// Create % load for fuel table using relative naturally aspirated cylinder filling
	float airChargeLoad = 100 * cylinderAirmass / getStandardAirCharge();
	if (!std::isfinite(cylinderAirmass) || !std::isfinite(airChargeLoad)) {
		return evaluation;
	}

	// Correct air mass by VE table
	AirmassInputs inputs;
	captureAirmassInputs(rpm, inputs);
	inputs.NativeLoad = airChargeLoad;
	inputs.Model = LM_REAL_MAF;
	auto ve = evaluateVe(inputs, airChargeLoad, diagnostics);
	mass_t correctedAirmass = cylinderAirmass * (ve.Ve * PERCENT_DIV);

	evaluation.Result = {
			correctedAirmass,
			airChargeLoad, // AFR/VE/ignition table Y axis
	};
	evaluation.Degraded = ve.Fallback;
	evaluation.Valid = ve.Valid && rpm > 0 && std::isfinite(massAirFlow) && massAirFlow >= 0 &&
					   std::isfinite(airChargeLoad) && std::isfinite(correctedAirmass) && correctedAirmass >= 0;
	if (evaluation.Valid) {
		evaluation.Valid = diagnostics.consumers(inputs, correctedAirmass);
	}
	if (!evaluation.Valid) {
		evaluation.Result.CylinderAirmass = 0;
	}
	return evaluation;
}

float MafAirmass::getVeImpl(float rpm, percent_t load) const {
	return interpolate3d(config->mafTable, config->mafLoadBins, load, config->mafRpmBins, rpm);
}
