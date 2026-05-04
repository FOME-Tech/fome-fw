#pragma once
#include "rusefi_types.h"
struct wall_fuel_state_s {
	// Wall fuel correction
	// mg
	// offset 0
	float wallFuelCorrection = (float)0;
	// Wall fuel amount
	// mg
	// offset 4
	float wallFuel = (float)0;
};
static_assert(sizeof(wall_fuel_state_s) == 8);
static_assert(offsetof(wall_fuel_state_s, wallFuelCorrection) == 0);
static_assert(offsetof(wall_fuel_state_s, wallFuel) == 4);
