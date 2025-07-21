#pragma once
#include "rusefi_types.h"
struct wall_fuel_state_s {
	// Wall fuel correction
	// mg
	// offset 0
	scaled_channel<int16_t, 10, 1> wallFuelCorrection = (int16_t)0;
	// Wall fuel amount
	// mg
	// offset 2
	scaled_channel<uint16_t, 10, 1> wallFuel = (uint16_t)0;
};
static_assert(sizeof(wall_fuel_state_s) == 4);
static_assert(offsetof(wall_fuel_state_s, wallFuelCorrection) == 0);
static_assert(offsetof(wall_fuel_state_s, wallFuel) == 2);

