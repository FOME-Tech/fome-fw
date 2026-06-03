#pragma once
#include "rusefi_types.h"
struct idle_state_s {
	// mightResetPid
	// The idea of 'mightResetPid' is to reset PID only once - each time when TPS > idlePidDeactivationTpsThreshold.
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
	// coasting
	// offset 0 bit 6
	bool isIdleCoasting : 1 {};
	// Closed loop active
	// offset 0 bit 7
	bool isIdleClosedLoop : 1 {};
	// offset 0 bit 8
	bool unusedBit_0_8 : 1 {};
	// offset 0 bit 9
	bool unusedBit_0_9 : 1 {};
	// offset 0 bit 10
	bool unusedBit_0_10 : 1 {};
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
	// Open loop: Lua Adder
	// offset 4
	float luaAdd = (float)0;
	// Open loop: iacByTpsTaper
	// offset 8
	float iacByTpsTaper = (float)0;
	// Open loop: iacByRpmTaper
	// offset 12
	float iacByRpmTaper = (float)0;
	// Open loop: Base
	// %
	// offset 16
	scaled_channel<uint8_t, 2, 1> openLoopBase = (uint8_t)0;
	// Open loop: AC bump
	// %
	// offset 17
	uint8_t openLoopAcBump = (uint8_t)0;
	// Open loop: Fan bump
	// %
	// offset 18
	uint8_t openLoopFanBump = (uint8_t)0;
	// Open loop
	// %
	// offset 19
	scaled_channel<uint8_t, 2, 1> openLoop = (uint8_t)0;
	// Closed loop
	// offset 20
	float idleClosedLoop = (float)0;
	// Position
	// %
	// offset 24
	float currentIdlePosition = (float)0;
	// Target airmass
	// mg
	// offset 28
	uint16_t idleTargetAirmass = (uint16_t)0;
	// Target airflow
	// kg/h
	// offset 30
	scaled_channel<uint16_t, 100, 1> idleTargetFlow = (uint16_t)0;
};
static_assert(sizeof(idle_state_s) == 32);
static_assert(offsetof(idle_state_s, luaAdd) == 4);
static_assert(offsetof(idle_state_s, iacByTpsTaper) == 8);
static_assert(offsetof(idle_state_s, iacByRpmTaper) == 12);
static_assert(offsetof(idle_state_s, openLoopBase) == 16);
static_assert(offsetof(idle_state_s, openLoopAcBump) == 17);
static_assert(offsetof(idle_state_s, openLoopFanBump) == 18);
static_assert(offsetof(idle_state_s, openLoop) == 19);
static_assert(offsetof(idle_state_s, idleClosedLoop) == 20);
static_assert(offsetof(idle_state_s, currentIdlePosition) == 24);
static_assert(offsetof(idle_state_s, idleTargetAirmass) == 28);
static_assert(offsetof(idle_state_s, idleTargetFlow) == 30);
