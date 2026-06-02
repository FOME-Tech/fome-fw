#pragma once
#include "rusefi_types.h"
struct torque_model_s {
	// Driver requested torque
	// Nm
	// offset 0
	float m_driverTorqueDemand = (float)0;
	// Requested torque
	// Nm
	// offset 4
	float m_torqueRequested = (float)0;
	// Requested torque (limited)
	// Nm
	// offset 8
	float m_torqueRequestedLimited = (float)0;
	// Torque loss
	// Nm
	// offset 12
	float m_torqueLoss = (float)0;
	// Loss table Y axis value
	// offset 16
	int16_t m_torqueLossLoadAxisValue = (int16_t)0;
	// offset 18
	uint8_t alignmentFill_at_18[2];
	// Gross torque
	// Nm
	// offset 20
	float m_grossTorque = (float)0;
	// Target cycle airmass
	// g
	// offset 24
	float m_airmassTarget = (float)0;
	// Measured cycle airmass
	// g
	// offset 28
	float m_airmassActual = (float)0;
	// Target airmass CL trim
	// %
	// offset 32
	float m_airmassTrim = (float)0;
	// Requested throttle position
	// %
	// offset 36
	float m_throttleRequest = (float)0;
	// Limited by engine max
	// offset 40 bit 0
	bool limitedByEngineMax : 1 {};
	// Limited by axle max
	// offset 40 bit 1
	bool limitedByAxleMax : 1 {};
	// offset 40 bit 2
	bool unusedBit_40_2 : 1 {};
	// offset 40 bit 3
	bool unusedBit_40_3 : 1 {};
	// offset 40 bit 4
	bool unusedBit_40_4 : 1 {};
	// offset 40 bit 5
	bool unusedBit_40_5 : 1 {};
	// offset 40 bit 6
	bool unusedBit_40_6 : 1 {};
	// offset 40 bit 7
	bool unusedBit_40_7 : 1 {};
	// offset 40 bit 8
	bool unusedBit_40_8 : 1 {};
	// offset 40 bit 9
	bool unusedBit_40_9 : 1 {};
	// offset 40 bit 10
	bool unusedBit_40_10 : 1 {};
	// offset 40 bit 11
	bool unusedBit_40_11 : 1 {};
	// offset 40 bit 12
	bool unusedBit_40_12 : 1 {};
	// offset 40 bit 13
	bool unusedBit_40_13 : 1 {};
	// offset 40 bit 14
	bool unusedBit_40_14 : 1 {};
	// offset 40 bit 15
	bool unusedBit_40_15 : 1 {};
	// offset 40 bit 16
	bool unusedBit_40_16 : 1 {};
	// offset 40 bit 17
	bool unusedBit_40_17 : 1 {};
	// offset 40 bit 18
	bool unusedBit_40_18 : 1 {};
	// offset 40 bit 19
	bool unusedBit_40_19 : 1 {};
	// offset 40 bit 20
	bool unusedBit_40_20 : 1 {};
	// offset 40 bit 21
	bool unusedBit_40_21 : 1 {};
	// offset 40 bit 22
	bool unusedBit_40_22 : 1 {};
	// offset 40 bit 23
	bool unusedBit_40_23 : 1 {};
	// offset 40 bit 24
	bool unusedBit_40_24 : 1 {};
	// offset 40 bit 25
	bool unusedBit_40_25 : 1 {};
	// offset 40 bit 26
	bool unusedBit_40_26 : 1 {};
	// offset 40 bit 27
	bool unusedBit_40_27 : 1 {};
	// offset 40 bit 28
	bool unusedBit_40_28 : 1 {};
	// offset 40 bit 29
	bool unusedBit_40_29 : 1 {};
	// offset 40 bit 30
	bool unusedBit_40_30 : 1 {};
	// offset 40 bit 31
	bool unusedBit_40_31 : 1 {};
};
static_assert(sizeof(torque_model_s) == 44);
static_assert(offsetof(torque_model_s, m_driverTorqueDemand) == 0);
static_assert(offsetof(torque_model_s, m_torqueRequested) == 4);
static_assert(offsetof(torque_model_s, m_torqueRequestedLimited) == 8);
static_assert(offsetof(torque_model_s, m_torqueLoss) == 12);
static_assert(offsetof(torque_model_s, m_torqueLossLoadAxisValue) == 16);
static_assert(offsetof(torque_model_s, m_grossTorque) == 20);
static_assert(offsetof(torque_model_s, m_airmassTarget) == 24);
static_assert(offsetof(torque_model_s, m_airmassActual) == 28);
static_assert(offsetof(torque_model_s, m_airmassTrim) == 32);
static_assert(offsetof(torque_model_s, m_throttleRequest) == 36);
