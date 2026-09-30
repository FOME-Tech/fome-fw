/**
 * @file	speed_density_base.h
 *
 * Base for speed density (ie, ideal gas law) math shared by multiple fueling modes.
 *
 * @date July 22, 2020
 * @author Matthew Kennedy, (C) 2020
 */

#pragma once

#include "airmass.h"

/**
 * @returns mass of air in cylinder
 */
mass_t idealGasLaw(float volume, float pressure, float temperature);

class SpeedDensityBase : public AirmassVeModelBase {
protected:
	explicit SpeedDensityBase(const ValueProvider3D* veTable, engine_load_mode_e model = LM_SPEED_DENSITY)
		: AirmassVeModelBase(veTable, model) {}

public:
	static mass_t getAirmassImpl(float ve, float manifoldPressure, float temperature);
	static mass_t
	getAirmassImpl(float ve, float manifoldPressure, float temperature, float displacement, float cylinderCount);
};
