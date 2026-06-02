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
	// Requested torque (limited)
	// Nm
	// offset 16
	float m_grossTorque = (float)0;
	// Target cycle airmass
	// g
	// offset 20
	float m_airmassTarget = (float)0;
	// Measured cycle airmass
	// g
	// offset 24
	float m_airmassActual = (float)0;
	// Target airmass CL trim
	// %
	// offset 28
	float m_airmassTrim = (float)0;
	// Requested throttle position
	// %
	// offset 32
	float m_throttleRequest = (float)0;
	// Limited by engine max
	// offset 36 bit 0
	bool limitedByEngineMax : 1 {};
	// Limited by axle max
	// offset 36 bit 1
	bool limitedByAxleMax : 1 {};
	// offset 36 bit 2
	bool unusedBit_36_2 : 1 {};
	// offset 36 bit 3
	bool unusedBit_36_3 : 1 {};
	// offset 36 bit 4
	bool unusedBit_36_4 : 1 {};
	// offset 36 bit 5
	bool unusedBit_36_5 : 1 {};
	// offset 36 bit 6
	bool unusedBit_36_6 : 1 {};
	// offset 36 bit 7
	bool unusedBit_36_7 : 1 {};
	// offset 36 bit 8
	bool unusedBit_36_8 : 1 {};
	// offset 36 bit 9
	bool unusedBit_36_9 : 1 {};
	// offset 36 bit 10
	bool unusedBit_36_10 : 1 {};
	// offset 36 bit 11
	bool unusedBit_36_11 : 1 {};
	// offset 36 bit 12
	bool unusedBit_36_12 : 1 {};
	// offset 36 bit 13
	bool unusedBit_36_13 : 1 {};
	// offset 36 bit 14
	bool unusedBit_36_14 : 1 {};
	// offset 36 bit 15
	bool unusedBit_36_15 : 1 {};
	// offset 36 bit 16
	bool unusedBit_36_16 : 1 {};
	// offset 36 bit 17
	bool unusedBit_36_17 : 1 {};
	// offset 36 bit 18
	bool unusedBit_36_18 : 1 {};
	// offset 36 bit 19
	bool unusedBit_36_19 : 1 {};
	// offset 36 bit 20
	bool unusedBit_36_20 : 1 {};
	// offset 36 bit 21
	bool unusedBit_36_21 : 1 {};
	// offset 36 bit 22
	bool unusedBit_36_22 : 1 {};
	// offset 36 bit 23
	bool unusedBit_36_23 : 1 {};
	// offset 36 bit 24
	bool unusedBit_36_24 : 1 {};
	// offset 36 bit 25
	bool unusedBit_36_25 : 1 {};
	// offset 36 bit 26
	bool unusedBit_36_26 : 1 {};
	// offset 36 bit 27
	bool unusedBit_36_27 : 1 {};
	// offset 36 bit 28
	bool unusedBit_36_28 : 1 {};
	// offset 36 bit 29
	bool unusedBit_36_29 : 1 {};
	// offset 36 bit 30
	bool unusedBit_36_30 : 1 {};
	// offset 36 bit 31
	bool unusedBit_36_31 : 1 {};
};
static_assert(sizeof(torque_model_s) == 40);
static_assert(offsetof(torque_model_s, m_driverTorqueDemand) == 0);
static_assert(offsetof(torque_model_s, m_torqueRequested) == 4);
static_assert(offsetof(torque_model_s, m_torqueRequestedLimited) == 8);
static_assert(offsetof(torque_model_s, m_torqueLoss) == 12);
static_assert(offsetof(torque_model_s, m_grossTorque) == 16);
static_assert(offsetof(torque_model_s, m_airmassTarget) == 20);
static_assert(offsetof(torque_model_s, m_airmassActual) == 24);
static_assert(offsetof(torque_model_s, m_airmassTrim) == 28);
static_assert(offsetof(torque_model_s, m_throttleRequest) == 32);
