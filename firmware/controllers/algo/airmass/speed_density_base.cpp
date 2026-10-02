/**
 * @file	speed_density_base.cpp
 *
 * Base for speed density (ie, ideal gas law) math shared by multiple fueling modes.
 *
 * @date July 22, 2020
 * @author Matthew Kennedy, (C) 2020
 */

#include "pch.h"
#include "speed_density_base.h"

/**
 * Derived via:
 * (8.31 J K mol^-1)  <- ideal gas constant R
 * /
 * (28.97g mol^-1)    <- molar mass of air
 * = 0.28705 J*K/g
 */
#define AIR_R 0.28705f

mass_t idealGasLaw(float volume, float pressure, float temperature) {
	return volume * pressure / (AIR_R * temperature);
}

/*static*/ mass_t SpeedDensityBase::getAirmassImpl(float ve, float manifoldPressure, float temperature) {
	return getAirmassImpl(
			ve, manifoldPressure, temperature, engineConfiguration->displacement, engine->engineState.cylinderCount);
}

/*static*/ mass_t SpeedDensityBase::getAirmassImpl(
		float ve, float manifoldPressure, float temperature, float displacement, float cylinderCount) {
	mass_t cycleAir = ve * idealGasLaw(displacement, manifoldPressure, temperature);
	return cycleAir / cylinderCount;
}
