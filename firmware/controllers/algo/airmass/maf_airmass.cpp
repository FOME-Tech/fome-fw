#include "pch.h"

#include "maf_airmass.h"
#include "maf.h"
#include "fuel_math.h"
#include "speed_density_base.h"

expected<float> MafAirmass::getMaf() const {
	auto maf = Sensor::get(SensorType::Maf);

	if (Sensor::hasSensor(SensorType::Maf2)) {
		auto maf2 = Sensor::get(SensorType::Maf2);

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
			return unexpected;
		}
	} else {
		return maf;
	}
}

// The most air a cylinder can hold at this RPM: the configured max load, as % of an ideal
// cylinder filled at baro pressure and intake air temperature.
static mass_t getMaxCylinderAirmass(float rpm) {
	float maxLoad = interpolate2d(rpm, config->mafMaxLoadRpmBins, config->mafMaxLoad);

	float baro = Sensor::get(SensorType::BarometricPressure).value_or(101.325f);
	float iat = Sensor::get(SensorType::Iat).value_or(20);

	float cylDisplacement = engineConfiguration->displacement / engine->engineState.cylinderCount;

	return maxLoad / 100 * idealGasLaw(cylDisplacement, baro, 273.15f + iat);
}

expected<AirmassResult> MafAirmass::getAirmass(float rpm, bool postState) {
	// If the engine is stopped, MAF is meaningless, and the intake manifold refills to atmospheric
	if (rpm == 0) {
		m_hasFilterState = false;
		return unexpected;
	}

	auto maf = getMaf();

	if (!maf) {
		return unexpected;
	}

	mass_t measuredAirmass = getMeasuredCylinderAirmass(maf.Value, rpm);

	// Only the fuel calculation advances the filter, other callers (Lua) see its current value
	mass_t cylinderAirmass = measuredAirmass;
	if (postState) {
		cylinderAirmass = filter(measuredAirmass, rpm);
	} else if (m_hasFilterState) {
		cylinderAirmass = m_filteredAirmass;
	}

	return makeResult(cylinderAirmass, rpm);
}

/**
 * The MAF measures air flowing in past the throttle, which isn't the same as air flowing out
 * into the cylinders while the intake manifold fills or empties. This filters the measured
 * per-cylinder airmass with a time constant of mafFilterParameter / RPM seconds, which models
 * the manifold: each intake event draws a fraction of the air stored in it.
 *
 * It filters airmass per cylinder event rather than flow, because that's what stays constant
 * when RPM changes at a fixed manifold pressure. Filtering flow instead under-reads badly when
 * RPM rises quickly (starting, when the manifold is still full of air at atmospheric pressure)
 * and over-reads when it falls quickly (a start that dies).
 *
 * The filter starts from the first measurement after the engine starts turning. While cranking,
 * the manifold is still at atmospheric pressure and the MAF reads what the cylinders draw.
 */
mass_t MafAirmass::filter(mass_t measuredAirmass, float rpm) {
	auto nowNt = getTimeNowNt();
	float dt = m_filterTimer.getElapsedSecondsAndReset(nowNt);

	float param = engineConfiguration->mafFilterParameter;

	if (!m_hasFilterState || param == 0) {
		m_hasFilterState = true;
		m_filteredAirmass = measuredAirmass;
		return m_filteredAirmass;
	}

	float alpha = std::clamp<float>(dt * rpm / param, 0, 1);
	m_filteredAirmass += alpha * (measuredAirmass - m_filteredAirmass);

	return m_filteredAirmass;
}

/**
 * Function block now works to create a standardised load from the cylinder filling as well as tune fuel via VE table.
 * @return total duration of fuel injection per engine cycle, in milliseconds
 */
expected<AirmassResult> MafAirmass::getAirmassImpl(float massAirFlow, float rpm) const {
	// If the engine is stopped, MAF is meaningless
	if (rpm == 0) {
		return unexpected;
	}

	return makeResult(getMeasuredCylinderAirmass(massAirFlow, rpm), rpm);
}

mass_t MafAirmass::getMeasuredCylinderAirmass(float massAirFlow, float rpm) const {
	// kg/hr -> g/s
	float gramPerSecond = massAirFlow * 1000 / 3600;

	// 1/min -> 1/s
	float revsPerSecond = rpm / 60.0f;
	mass_t airPerRevolution = gramPerSecond / revsPerSecond;

	// Now we have to divide among cylinders - on a 4 stroke, half of the cylinders happen every revolution
	// This math is floating point to work properly on engines with odd cylinder count
	float halfCylCount = engine->engineState.cylinderCount / 2.0f;

	mass_t cylinderAirmass = airPerRevolution / halfCylCount;

	// MAF/RPM can read far more than the cylinder can physically hold: while the intake manifold
	// is filling (tip-in), and when RPM drops faster than the MAF reading does (a start that dies)
	mass_t maxAirmass = getMaxCylinderAirmass(rpm);
	bool limited = cylinderAirmass > maxAirmass;
	engine->outputChannels.mafAirmassLimited = limited;
	if (limited) {
		cylinderAirmass = maxAirmass;
	}

	return cylinderAirmass;
}

AirmassResult MafAirmass::makeResult(mass_t cylinderAirmass, float rpm) const {
	// Create % load for fuel table using relative naturally aspirated cylinder filling
	float airChargeLoad = 100 * cylinderAirmass / getStandardAirCharge();

	// Correct air mass by MAF trim table
	mass_t correctedAirmass =
			cylinderAirmass *
			interpolate3d(config->mafTrimTable, config->mafTrimLoadBins, airChargeLoad, config->mafTrimRpmBins, rpm) *
			PERCENT_DIV;

	return AirmassResult{
			correctedAirmass,
			airChargeLoad, // AFR/VE/ignition table Y axis
	};
}
