/**
 * @file map.cpp
 *
 * See also map_averaging.cpp
 *
 * @author Andrey Belomutskiy, (c) 2012-2020
 */
#include "pch.h"

/**
 * This function checks if Baro/MAP sensor value is inside of expected range
 * @return unchanged mapKPa parameter or NaN
 */
static float validateBaroMap(float mapKPa) {
	// Highest interstate is the Eisenhower Tunnel at 11158 feet -> 66 kpa
	// Lowest point is the Dead Sea, -1411 feet -> 106 kpa
	if (std::isnan(mapKPa) || mapKPa > 110 || mapKPa < 60) {
		warning(ObdCode::OBD_Barometric_Press_Circ, "Invalid start-up baro pressure = %.2fkPa", mapKPa);
		return NAN;
	}
	return mapKPa;
}

// Set once the power-up MAP reading has been taken (or rejected), so it's only attempted once.
static bool fixedBaroDone = false;

void initMapDecoder() {
	// The slow ADC hasn't produced a sample yet at init, so the capture itself happens in
	// updateFixedBaroFromMap() once MAP is valid.
	fixedBaroDone = false;
}

static bool isEngineTurning() {
#if EFI_SHAFT_POSITION_INPUT
	if (engine->triggerCentral.engineMovedRecently()) {
		return true;
	}
#endif // EFI_SHAFT_POSITION_INPUT

	return Sensor::getOrZero(SensorType::Rpm) > 0;
}

void updateFixedBaroFromMap() {
	if (fixedBaroDone || !engineConfiguration->useFixedBaroCorrFromMap) {
		return;
	}

	auto map = Sensor::get(SensorType::MapSlow);
	if (!map) {
		// No MAP reading yet, keep waiting. Baro meanwhile comes from any other baro sensor, or
		// is absent so consumers fall back to standard atmosphere.
		return;
	}

	// One shot: whatever happens below, don't try again until the next restart.
	fixedBaroDone = true;

	if (isEngineTurning()) {
		// Manifold isn't at atmospheric pressure if the engine is turning
		warning(ObdCode::OBD_Barometric_Press_Circ, "Engine turning at start-up, MAP not used as baro");
		efiPrintf("Engine already turning, the fixed baro correction will be disabled!");
		return;
	}

	efiPrintf("Get initial baro MAP pressure = %.2fkPa", map.Value);
	// validate if it's within a reasonable range
	float storedInitialBaroPressure = validateBaroMap(map.Value);

	if (!std::isnan(storedInitialBaroPressure)) {
		efiPrintf("Using this fixed MAP pressure to override the baro correction!");

		// A mock overrides whatever baro sensor (analog or onboard) is registered, which is exactly
		// what this option is documented to do.
		Sensor::setMockValue(SensorType::BarometricPressure, storedInitialBaroPressure);
	} else {
		efiPrintf("The baro pressure is invalid. The fixed baro correction will be disabled!");
	}
}
