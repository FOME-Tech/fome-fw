#pragma once
#include "rusefi_types.h"
struct ignition_state_s {
	// ms
	// offset 0
	float baseDwell = (float)0;
	// Ign: Dwell
	// ms
	// offset 4
	float sparkDwell = (float)0;
	// Ign: Dwell angle
	// deg
	// offset 8
	angle_t dwellAngle = (angle_t)0;
	// Ign: CLT correction
	// deg
	// offset 12
	scaled_channel<int16_t, 100, 1> cltTimingCorrection = (int16_t)0;
	// Ign: IAT correction
	// deg
	// offset 14
	scaled_channel<int16_t, 100, 1> timingIatCorrection = (int16_t)0;
	// Idle: Timing adjustment
	// deg
	// offset 16
	scaled_channel<int16_t, 100, 1> timingPidCorrection = (int16_t)0;
	// DFCO: Timing retard
	// deg
	// offset 18
	scaled_channel<int16_t, 100, 1> dfcoTimingRetard = (int16_t)0;
	// Ign: Dwell voltage correction
	// offset 20
	float dwellVoltageCorrection = (float)0;
	// Ign: Lua timing add
	// deg
	// offset 24
	float luaTimingAdd = (float)0;
	// Ign: Lua timing mult
	// deg
	// offset 28
	float luaTimingMult = (float)0;
};
static_assert(sizeof(ignition_state_s) == 32);
static_assert(offsetof(ignition_state_s, baseDwell) == 0);
static_assert(offsetof(ignition_state_s, sparkDwell) == 4);
static_assert(offsetof(ignition_state_s, dwellAngle) == 8);
static_assert(offsetof(ignition_state_s, cltTimingCorrection) == 12);
static_assert(offsetof(ignition_state_s, timingIatCorrection) == 14);
static_assert(offsetof(ignition_state_s, timingPidCorrection) == 16);
static_assert(offsetof(ignition_state_s, dfcoTimingRetard) == 18);
static_assert(offsetof(ignition_state_s, dwellVoltageCorrection) == 20);
static_assert(offsetof(ignition_state_s, luaTimingAdd) == 24);
static_assert(offsetof(ignition_state_s, luaTimingMult) == 28);

