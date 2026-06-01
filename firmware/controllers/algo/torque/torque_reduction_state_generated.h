#pragma once
#include "rusefi_types.h"
struct torque_reduction_state_s {
	// Reduction request
	// frac
	// offset 0
	float reductionRequest = (float)0;
	// Timing retard
	// deg
	// offset 4
	float retardApplied = (float)0;
	// Cylinder cut fraction
	// frac
	// offset 8
	float cutFraction = (float)0;
};
static_assert(sizeof(torque_reduction_state_s) == 12);
static_assert(offsetof(torque_reduction_state_s, reductionRequest) == 0);
static_assert(offsetof(torque_reduction_state_s, retardApplied) == 4);
static_assert(offsetof(torque_reduction_state_s, cutFraction) == 8);
