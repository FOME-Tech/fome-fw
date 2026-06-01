#pragma once
#include "rusefi_types.h"
struct torque_model_s {
	// Driver requested torque
	// Nm
	// offset 0
	float driverTorqueDemand = (float)0;
};
static_assert(sizeof(torque_model_s) == 4);
static_assert(offsetof(torque_model_s, driverTorqueDemand) == 0);
