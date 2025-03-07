#pragma once
#include "rusefi_types.h"
struct pid_status_2_s {
	// offset 0
	int16_t pTerm = (int16_t)0;
	// offset 2
	scaled_channel<int16_t, 100, 1> iTerm = (int16_t)0;
	// offset 4
	scaled_channel<int16_t, 100, 1> dTerm = (int16_t)0;
	// offset 6
	scaled_channel<int16_t, 100, 1> output = (int16_t)0;
	// offset 8
	scaled_channel<int16_t, 100, 1> error = (int16_t)0;
	// offset 10
	uint16_t resetCounter = (uint16_t)0;
};
static_assert(sizeof(pid_status_2_s) == 12);
static_assert(offsetof(pid_status_2_s, pTerm) == 0);
static_assert(offsetof(pid_status_2_s, iTerm) == 2);
static_assert(offsetof(pid_status_2_s, dTerm) == 4);
static_assert(offsetof(pid_status_2_s, output) == 6);
static_assert(offsetof(pid_status_2_s, error) == 8);
static_assert(offsetof(pid_status_2_s, resetCounter) == 10);

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
	// offset 4
	pid_status_2_s pidState;
};
static_assert(sizeof(vvt_s) == 16);
static_assert(offsetof(vvt_s, vvtTarget) == 0);
static_assert(offsetof(vvt_s, vvtOutput) == 2);

