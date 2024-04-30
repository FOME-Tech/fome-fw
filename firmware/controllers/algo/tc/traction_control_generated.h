#pragma once
#include "rusefi_types.h"
struct traction_control_s {
	// TC: Eng actual tq
	// offset 0
	float engineTorqueActual = (float)0;
	// TC: Driver req tq
	// offset 4
	float driverReqTorque = (float)0;
	// TC: Eng min tq
	// offset 8
	float engineTorqueMin = (float)0;
	// TC: Eng max tq
	// offset 12
	float engineTorqueMax = (float)0;
	// TC: Current gear
	// offset 16
	uint8_t currentGear = (uint8_t)0;
	// offset 17
	uint8_t alignmentFill_at_17[3];
	// TC: WSS Front
	// offset 20
	float wssFront = (float)0;
	// TC: WSS Rear
	// offset 24
	float wssRear = (float)0;
	// offset 28 bit 0
	bool engineTorqueActualValid : 1 {};
	// offset 28 bit 1
	bool driverReqTorqueValid : 1 {};
	// offset 28 bit 2
	bool engineTorqueMinValid : 1 {};
	// offset 28 bit 3
	bool engineTorqueMaxValid : 1 {};
	// offset 28 bit 4
	bool currentGearValid : 1 {};
	// TC: Active
	// offset 28 bit 5
	bool tcActive : 1 {};
	// offset 28 bit 6
	bool unusedBit_28_6 : 1 {};
	// offset 28 bit 7
	bool unusedBit_28_7 : 1 {};
	// offset 28 bit 8
	bool unusedBit_28_8 : 1 {};
	// offset 28 bit 9
	bool unusedBit_28_9 : 1 {};
	// offset 28 bit 10
	bool unusedBit_28_10 : 1 {};
	// offset 28 bit 11
	bool unusedBit_28_11 : 1 {};
	// offset 28 bit 12
	bool unusedBit_28_12 : 1 {};
	// offset 28 bit 13
	bool unusedBit_28_13 : 1 {};
	// offset 28 bit 14
	bool unusedBit_28_14 : 1 {};
	// offset 28 bit 15
	bool unusedBit_28_15 : 1 {};
	// offset 28 bit 16
	bool unusedBit_28_16 : 1 {};
	// offset 28 bit 17
	bool unusedBit_28_17 : 1 {};
	// offset 28 bit 18
	bool unusedBit_28_18 : 1 {};
	// offset 28 bit 19
	bool unusedBit_28_19 : 1 {};
	// offset 28 bit 20
	bool unusedBit_28_20 : 1 {};
	// offset 28 bit 21
	bool unusedBit_28_21 : 1 {};
	// offset 28 bit 22
	bool unusedBit_28_22 : 1 {};
	// offset 28 bit 23
	bool unusedBit_28_23 : 1 {};
	// offset 28 bit 24
	bool unusedBit_28_24 : 1 {};
	// offset 28 bit 25
	bool unusedBit_28_25 : 1 {};
	// offset 28 bit 26
	bool unusedBit_28_26 : 1 {};
	// offset 28 bit 27
	bool unusedBit_28_27 : 1 {};
	// offset 28 bit 28
	bool unusedBit_28_28 : 1 {};
	// offset 28 bit 29
	bool unusedBit_28_29 : 1 {};
	// offset 28 bit 30
	bool unusedBit_28_30 : 1 {};
	// offset 28 bit 31
	bool unusedBit_28_31 : 1 {};
	// TC: Slip
	// offset 32
	float slipRate = (float)0;
	// TC: Slip error
	// offset 36
	float m_slipError = (float)0;
	// TC: requested torque
	// offset 40
	float torqueRequest = (float)0;
};
static_assert(sizeof(traction_control_s) == 44);
static_assert(offsetof(traction_control_s, engineTorqueActual) == 0);
static_assert(offsetof(traction_control_s, driverReqTorque) == 4);
static_assert(offsetof(traction_control_s, engineTorqueMin) == 8);
static_assert(offsetof(traction_control_s, engineTorqueMax) == 12);
static_assert(offsetof(traction_control_s, currentGear) == 16);
static_assert(offsetof(traction_control_s, wssFront) == 20);
static_assert(offsetof(traction_control_s, wssRear) == 24);
static_assert(offsetof(traction_control_s, slipRate) == 32);
static_assert(offsetof(traction_control_s, m_slipError) == 36);
static_assert(offsetof(traction_control_s, torqueRequest) == 40);

