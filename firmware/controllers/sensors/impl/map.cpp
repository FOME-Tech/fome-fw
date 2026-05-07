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

void initMapDecoder() {
	if (engineConfiguration->useFixedBaroCorrFromMap) {
		// Read initial MAP sensor value and store it for Baro correction.
		float storedInitialBaroPressure = Sensor::get(SensorType::MapSlow).value_or(101.325);
		efiPrintf("Get initial baro MAP pressure = %.2fkPa", storedInitialBaroPressure);
		// validate if it's within a reasonable range (the engine should not be spinning etc.)
		storedInitialBaroPressure = validateBaroMap(storedInitialBaroPressure);
		if (!std::isnan(storedInitialBaroPressure)) {
			efiPrintf("Using this fixed MAP pressure to override the baro correction!");

			// TODO: do literally anything other than this
			Sensor::setMockValue(SensorType::BarometricPressure, storedInitialBaroPressure);
		} else {
			efiPrintf("The baro pressure is invalid. The fixed baro correction will be disabled!");
		}
	}
}
