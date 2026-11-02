#pragma once
#include "rusefi_types.h"
struct gear_detector_state_s {
	// Gearbox Ratio
	// value
	// offset 0
	scaled_channel<uint16_t, 100, 1> speedToRpmRatio = (uint16_t)0;
	// Detected Gear
	// offset 2
	uint8_t detectedGear = (uint8_t)0;
	// offset 3
	uint8_t alignmentFill_at_3[1];
};
static_assert(sizeof(gear_detector_state_s) == 4);
static_assert(offsetof(gear_detector_state_s, speedToRpmRatio) == 0);
static_assert(offsetof(gear_detector_state_s, detectedGear) == 2);
