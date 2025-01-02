#pragma once
#include "rusefi_types.h"
struct vvt_s {
	// Target
	// deg
	// offset 0
	scaled_channel<uint16_t, 10, 1> vvtTarget = (uint16_t)0;
	// Output duty
	// %
	// offset 2
	scaled_channel<uint8_t, 2, 1> vvtOutput = (uint8_t)0;
	// offset 3
	uint8_t alignmentFill_at_3[1];
};
static_assert(sizeof(vvt_s) == 4);
static_assert(offsetof(vvt_s, vvtTarget) == 0);
static_assert(offsetof(vvt_s, vvtOutput) == 2);

