#pragma once
#include "rusefi_types.h"
struct torque_model_s {
	// Driver requested torque
	// Nm
	// offset 0
	float driverTorqueDemand = (float)0;
	// Target airmass per engine cycle (whole engine)
	// g
	// offset 4
	float airmassTarget = (float)0;
	// Measured airmass per engine cycle (whole engine)
	// g
	// offset 8
	float airmassActual = (float)0;
	// Closed-loop airmass trim applied to the commanded throttle flow
	// %
	// offset 12
	float airmassTrim = (float)0;
	// Torque-model requested throttle position
	// %
	// offset 16
	float throttleRequest = (float)0;
};
static_assert(sizeof(torque_model_s) == 20);
static_assert(offsetof(torque_model_s, driverTorqueDemand) == 0);
static_assert(offsetof(torque_model_s, airmassTarget) == 4);
static_assert(offsetof(torque_model_s, airmassActual) == 8);
static_assert(offsetof(torque_model_s, airmassTrim) == 12);
static_assert(offsetof(torque_model_s, throttleRequest) == 16);
