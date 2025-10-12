#pragma once
#include "rusefi_types.h"
struct idle_state_s {
	// mightResetPid
	// The idea of 'mightResetPid' is to reset PID only once - each time when TPS > idlePidDeactivationTpsThreshold.
	// The throttle pedal can be pressed for a long time, making the PID data obsolete (thus the reset is required).
	// We set 'mightResetPid' to true only if PID was actually used (i.e. idlePid.getOutput() was called) to save some CPU resources.
	// See automaticIdleController().
	// offset 0 bit 0
	bool mightResetPid : 1 {};
	// wasResetPid
	// This is needed to slowly turn on the PID back after it was reset.
	// offset 0 bit 1
	bool wasResetPid : 1 {};
	// cranking
	// offset 0 bit 2
	bool isCranking : 1 {};
	// offset 0 bit 3
	bool isIacTableForCoasting : 1 {};
	// offset 0 bit 4
	bool notIdling : 1 {};
	// offset 0 bit 5
	bool isBlipping : 1 {};
	// offset 0 bit 6
	bool looksLikeRunning : 1 {};
	// offset 0 bit 7
	bool looksLikeCoasting : 1 {};
	// offset 0 bit 8
	bool looksLikeCrankToIdle : 1 {};
	// coasting
	// offset 0 bit 9
	bool isIdleCoasting : 1 {};
	// Closed loop active
	// offset 0 bit 10
	bool isIdleClosedLoop : 1 {};
	// offset 0 bit 11
	bool unusedBit_0_11 : 1 {};
	// offset 0 bit 12
	bool unusedBit_0_12 : 1 {};
	// offset 0 bit 13
	bool unusedBit_0_13 : 1 {};
	// offset 0 bit 14
	bool unusedBit_0_14 : 1 {};
	// offset 0 bit 15
	bool unusedBit_0_15 : 1 {};
	// offset 0 bit 16
	bool unusedBit_0_16 : 1 {};
	// offset 0 bit 17
	bool unusedBit_0_17 : 1 {};
	// offset 0 bit 18
	bool unusedBit_0_18 : 1 {};
	// offset 0 bit 19
	bool unusedBit_0_19 : 1 {};
	// offset 0 bit 20
	bool unusedBit_0_20 : 1 {};
	// offset 0 bit 21
	bool unusedBit_0_21 : 1 {};
	// offset 0 bit 22
	bool unusedBit_0_22 : 1 {};
	// offset 0 bit 23
	bool unusedBit_0_23 : 1 {};
	// offset 0 bit 24
	bool unusedBit_0_24 : 1 {};
	// offset 0 bit 25
	bool unusedBit_0_25 : 1 {};
	// offset 0 bit 26
	bool unusedBit_0_26 : 1 {};
	// offset 0 bit 27
	bool unusedBit_0_27 : 1 {};
	// offset 0 bit 28
	bool unusedBit_0_28 : 1 {};
	// offset 0 bit 29
	bool unusedBit_0_29 : 1 {};
	// offset 0 bit 30
	bool unusedBit_0_30 : 1 {};
	// offset 0 bit 31
	bool unusedBit_0_31 : 1 {};
	// Target RPM: Base
	// offset 4
	uint16_t targetRpmByClt = (uint16_t)0;
	// Target RPM: A/C bump
	// offset 6
	uint16_t targetRpmAcBump = (uint16_t)0;
	// Target RPM: Lua adder
	// offset 8
	float luaAddRpm = (float)0;
	// Target RPM
	// offset 12
	uint16_t idleTarget = (uint16_t)0;
	// Entry threshold
	// offset 14
	uint16_t idleEntryRpm = (uint16_t)0;
	// Exit threshold
	// offset 16
	uint16_t idleExitRpm = (uint16_t)0;
	// offset 18
	uint8_t alignmentFill_at_18[2];
	// Open loop: Lua Adder
	// offset 20
	float luaAdd = (float)0;
	// Open loop: iacByTpsTaper
	// offset 24
	float iacByTpsTaper = (float)0;
	// Open loop: iacByRpmTaper
	// offset 28
	float iacByRpmTaper = (float)0;
	// Open loop: Base
	// %
	// offset 32
	scaled_channel<uint8_t, 2, 1> openLoopBase = (uint8_t)0;
	// Open loop: AC bump
	// %
	// offset 33
	uint8_t openLoopAcBump = (uint8_t)0;
	// Open loop: Fan bump
	// %
	// offset 34
	uint8_t openLoopFanBump = (uint8_t)0;
	// Open loop
	// %
	// offset 35
	scaled_channel<uint8_t, 2, 1> openLoop = (uint8_t)0;
	// Closed loop
	// offset 36
	float idleClosedLoop = (float)0;
	// Position
	// %
	// offset 40
	float currentIdlePosition = (float)0;
	// Target airmass
	// mg
	// offset 44
	uint16_t idleTargetAirmass = (uint16_t)0;
	// Target airflow
	// kg/h
	// offset 46
	scaled_channel<uint16_t, 100, 1> idleTargetFlow = (uint16_t)0;
};
static_assert(sizeof(idle_state_s) == 48);
static_assert(offsetof(idle_state_s, targetRpmByClt) == 4);
static_assert(offsetof(idle_state_s, targetRpmAcBump) == 6);
static_assert(offsetof(idle_state_s, luaAddRpm) == 8);
static_assert(offsetof(idle_state_s, idleTarget) == 12);
static_assert(offsetof(idle_state_s, idleEntryRpm) == 14);
static_assert(offsetof(idle_state_s, idleExitRpm) == 16);
static_assert(offsetof(idle_state_s, luaAdd) == 20);
static_assert(offsetof(idle_state_s, iacByTpsTaper) == 24);
static_assert(offsetof(idle_state_s, iacByRpmTaper) == 28);
static_assert(offsetof(idle_state_s, openLoopBase) == 32);
static_assert(offsetof(idle_state_s, openLoopAcBump) == 33);
static_assert(offsetof(idle_state_s, openLoopFanBump) == 34);
static_assert(offsetof(idle_state_s, openLoop) == 35);
static_assert(offsetof(idle_state_s, idleClosedLoop) == 36);
static_assert(offsetof(idle_state_s, currentIdlePosition) == 40);
static_assert(offsetof(idle_state_s, idleTargetAirmass) == 44);
static_assert(offsetof(idle_state_s, idleTargetFlow) == 46);

