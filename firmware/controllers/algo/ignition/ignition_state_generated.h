#pragma once
#include "rusefi_types.h"
struct ignition_state_s {
	// Ign: Dwell
	// ms
	// offset 0
	float sparkDwell = (float)0;
	// Ign: Dwell angle
	// deg
	// offset 4
	angle_t dwellAngle = (angle_t)0;
	// Ign: CLT correction
	// deg
	// offset 8
	scaled_channel<int16_t, 100, 1> cltTimingCorrection = (int16_t)0;
	// Ign: IAT correction
	// deg
	// offset 10
	scaled_channel<int16_t, 100, 1> timingIatCorrection = (int16_t)0;
	// Idle: Timing adjustment
	// deg
	// offset 12
	scaled_channel<int16_t, 100, 1> timingPidCorrection = (int16_t)0;
	// DFCO: Timing retard
	// deg
	// offset 14
	scaled_channel<int16_t, 100, 1> dfcoTimingRetard = (int16_t)0;
	// Ign: Lua timing add
	// deg
	// offset 16
	float luaTimingAdd = (float)0;
	// Ign: Lua timing mult
	// deg
	// offset 20
	float luaTimingMult = (float)0;
};
static_assert(sizeof(ignition_state_s) == 24);
static_assert(offsetof(ignition_state_s, sparkDwell) == 0);
static_assert(offsetof(ignition_state_s, dwellAngle) == 4);
static_assert(offsetof(ignition_state_s, cltTimingCorrection) == 8);
static_assert(offsetof(ignition_state_s, timingIatCorrection) == 10);
static_assert(offsetof(ignition_state_s, timingPidCorrection) == 12);
static_assert(offsetof(ignition_state_s, dfcoTimingRetard) == 14);
static_assert(offsetof(ignition_state_s, luaTimingAdd) == 16);
static_assert(offsetof(ignition_state_s, luaTimingMult) == 20);
