#pragma once
#include "rusefi_types.h"
struct torque_reduction_state_s {
	// Reduction request
	// %
	// offset 0
	scaled_channel<uint8_t, 2, 1> reductionRequest = (uint8_t)0;
	// Timing retard
	// deg
	// offset 1
	scaled_channel<uint8_t, 2, 1> retardApplied = (uint8_t)0;
	// Cylinder cut fraction
	// %
	// offset 2
	uint8_t cutFraction = (uint8_t)0;
	// offset 3
	uint8_t alignmentFill_at_3[1];
};
static_assert(sizeof(torque_reduction_state_s) == 4);
static_assert(offsetof(torque_reduction_state_s, reductionRequest) == 0);
static_assert(offsetof(torque_reduction_state_s, retardApplied) == 1);
static_assert(offsetof(torque_reduction_state_s, cutFraction) == 2);
