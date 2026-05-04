#pragma once
#include "rusefi_types.h"
struct boost_control_s {
	// Below CL thresh
	// offset 0 bit 0
	bool isBelowClosedLoopThreshold : 1 {};
	// Zero RPM
	// offset 0 bit 1
	bool isZeroRpm : 1 {};
	// RPM too low
	// offset 0 bit 2
	bool rpmTooLow : 1 {};
	// TPS too low
	// offset 0 bit 3
	bool tpsTooLow : 1 {};
	// MAP too low
	// offset 0 bit 4
	bool mapTooLow : 1 {};
	// offset 0 bit 5
	bool unusedBit_0_5 : 1 {};
	// offset 0 bit 6
	bool unusedBit_0_6 : 1 {};
	// offset 0 bit 7
	bool unusedBit_0_7 : 1 {};
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
	// Lua target mult
	// offset 4
	float luaTargetMult = (float)0;
	// Lua target add
	// percent
	// offset 8
	scaled_channel<int16_t, 2, 1> luaTargetAdd = (int16_t)0;
	// Target
	// kPa
	// offset 10
	scaled_channel<int16_t, 30, 1> boostControlTarget = (int16_t)0;
	// Lua open loop add
	// percent
	// offset 12
	float luaOpenLoopAdd = (float)0;
	// Open loop
	// percent
	// offset 16
	scaled_channel<uint8_t, 2, 1> openLoopPart = (uint8_t)0;
	// Closed loop
	// %
	// offset 17
	scaled_channel<int8_t, 2, 1> boostControllerClosedLoopPart = (int8_t)0;
	// Output
	// percent
	// offset 18
	scaled_channel<int16_t, 100, 1> boostOutput = (int16_t)0;
	// offset 20
	scaled_channel<int16_t, 10, 1> boostOpenLoopBlendParameter[2];
	// %
	// offset 24
	scaled_channel<uint8_t, 2, 1> boostOpenLoopBlendBias[2];
	// %
	// offset 26
	int8_t boostOpenLoopBlendOutput[2];
	// offset 28
	int16_t boostOpenLoopBlendYAxis[2];
	// offset 32
	scaled_channel<int16_t, 10, 1> boostClosedLoopBlendParameter[2];
	// %
	// offset 36
	scaled_channel<uint8_t, 2, 1> boostClosedLoopBlendBias[2];
	// %
	// offset 38
	scaled_channel<int16_t, 10, 1> boostClosedLoopBlendOutput[2];
	// offset 42
	int16_t boostClosedLoopBlendYAxis[2];
	// offset 46
	int16_t boostOpenLoopXAxisValue = (int16_t)0;
	// offset 48
	int16_t boostOpenLoopYAxisValue = (int16_t)0;
	// offset 50
	int16_t boostClosedLoopXAxisValue = (int16_t)0;
	// offset 52
	int16_t boostClosedLoopYAxisValue = (int16_t)0;
	// offset 54
	uint8_t alignmentFill_at_54[2];
};
static_assert(sizeof(boost_control_s) == 56);
static_assert(offsetof(boost_control_s, luaTargetMult) == 4);
static_assert(offsetof(boost_control_s, luaTargetAdd) == 8);
static_assert(offsetof(boost_control_s, boostControlTarget) == 10);
static_assert(offsetof(boost_control_s, luaOpenLoopAdd) == 12);
static_assert(offsetof(boost_control_s, openLoopPart) == 16);
static_assert(offsetof(boost_control_s, boostControllerClosedLoopPart) == 17);
static_assert(offsetof(boost_control_s, boostOutput) == 18);
static_assert(offsetof(boost_control_s, boostOpenLoopBlendParameter) == 20);
static_assert(offsetof(boost_control_s, boostOpenLoopBlendBias) == 24);
static_assert(offsetof(boost_control_s, boostOpenLoopBlendOutput) == 26);
static_assert(offsetof(boost_control_s, boostOpenLoopBlendYAxis) == 28);
static_assert(offsetof(boost_control_s, boostClosedLoopBlendParameter) == 32);
static_assert(offsetof(boost_control_s, boostClosedLoopBlendBias) == 36);
static_assert(offsetof(boost_control_s, boostClosedLoopBlendOutput) == 38);
static_assert(offsetof(boost_control_s, boostClosedLoopBlendYAxis) == 42);
static_assert(offsetof(boost_control_s, boostOpenLoopXAxisValue) == 46);
static_assert(offsetof(boost_control_s, boostOpenLoopYAxisValue) == 48);
static_assert(offsetof(boost_control_s, boostClosedLoopXAxisValue) == 50);
static_assert(offsetof(boost_control_s, boostClosedLoopYAxisValue) == 52);
