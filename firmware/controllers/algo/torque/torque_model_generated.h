#pragma once
#include "rusefi_types.h"
struct torque_model_s {
	// Driver requested torque
	// Nm
	// offset 0
	scaled_channel<int16_t, 10, 1> m_driverTorqueDemand = (int16_t)0;
	// Requested torque
	// Nm
	// offset 2
	scaled_channel<int16_t, 10, 1> m_torqueRequested = (int16_t)0;
	// Requested torque (limited)
	// Nm
	// offset 4
	scaled_channel<int16_t, 10, 1> m_torqueRequestedLimited = (int16_t)0;
	// Torque loss
	// Nm
	// offset 6
	scaled_channel<int16_t, 10, 1> m_torqueLoss = (int16_t)0;
	// Loss table Y axis value
	// offset 8
	int16_t m_torqueLossLoadAxisValue = (int16_t)0;
	// Gross torque
	// Nm
	// offset 10
	scaled_channel<int16_t, 10, 1> m_grossTorque = (int16_t)0;
	// Target cycle airmass
	// g
	// offset 12
	scaled_channel<uint16_t, 1000, 1> m_airmassTarget = (uint16_t)0;
	// Measured cycle airmass
	// g
	// offset 14
	scaled_channel<uint16_t, 1000, 1> m_airmassActual = (uint16_t)0;
	// Target airmass CL trim
	// %
	// offset 16
	scaled_channel<uint16_t, 100, 1> m_airmassTrim = (uint16_t)0;
	// Requested throttle position
	// %
	// offset 18
	scaled_channel<uint16_t, 100, 1> m_throttleRequest = (uint16_t)0;
	// Generic limiter X axis
	// offset 20
	int16_t m_limiterXAxisValue[4];
	// Generic limiter Y axis
	// offset 28
	int16_t m_limiterYAxisValue[4];
	// Generic limiter ceiling
	// Nm
	// offset 36
	uint16_t m_limiterTorque[4];
	// Limited by engine max
	// offset 44 bit 0
	bool limitedByEngineMax : 1 {};
	// Limited by axle max
	// offset 44 bit 1
	bool limitedByAxleMax : 1 {};
	// Limited by generic limiter 1
	// offset 44 bit 2
	bool limitedByGenericLimiter1 : 1 {};
	// Limited by generic limiter 2
	// offset 44 bit 3
	bool limitedByGenericLimiter2 : 1 {};
	// Limited by generic limiter 3
	// offset 44 bit 4
	bool limitedByGenericLimiter3 : 1 {};
	// Limited by generic limiter 4
	// offset 44 bit 5
	bool limitedByGenericLimiter4 : 1 {};
	// offset 44 bit 6
	bool unusedBit_44_6 : 1 {};
	// offset 44 bit 7
	bool unusedBit_44_7 : 1 {};
	// offset 44 bit 8
	bool unusedBit_44_8 : 1 {};
	// offset 44 bit 9
	bool unusedBit_44_9 : 1 {};
	// offset 44 bit 10
	bool unusedBit_44_10 : 1 {};
	// offset 44 bit 11
	bool unusedBit_44_11 : 1 {};
	// offset 44 bit 12
	bool unusedBit_44_12 : 1 {};
	// offset 44 bit 13
	bool unusedBit_44_13 : 1 {};
	// offset 44 bit 14
	bool unusedBit_44_14 : 1 {};
	// offset 44 bit 15
	bool unusedBit_44_15 : 1 {};
	// offset 44 bit 16
	bool unusedBit_44_16 : 1 {};
	// offset 44 bit 17
	bool unusedBit_44_17 : 1 {};
	// offset 44 bit 18
	bool unusedBit_44_18 : 1 {};
	// offset 44 bit 19
	bool unusedBit_44_19 : 1 {};
	// offset 44 bit 20
	bool unusedBit_44_20 : 1 {};
	// offset 44 bit 21
	bool unusedBit_44_21 : 1 {};
	// offset 44 bit 22
	bool unusedBit_44_22 : 1 {};
	// offset 44 bit 23
	bool unusedBit_44_23 : 1 {};
	// offset 44 bit 24
	bool unusedBit_44_24 : 1 {};
	// offset 44 bit 25
	bool unusedBit_44_25 : 1 {};
	// offset 44 bit 26
	bool unusedBit_44_26 : 1 {};
	// offset 44 bit 27
	bool unusedBit_44_27 : 1 {};
	// offset 44 bit 28
	bool unusedBit_44_28 : 1 {};
	// offset 44 bit 29
	bool unusedBit_44_29 : 1 {};
	// offset 44 bit 30
	bool unusedBit_44_30 : 1 {};
	// offset 44 bit 31
	bool unusedBit_44_31 : 1 {};
};
static_assert(sizeof(torque_model_s) == 48);
static_assert(offsetof(torque_model_s, m_driverTorqueDemand) == 0);
static_assert(offsetof(torque_model_s, m_torqueRequested) == 2);
static_assert(offsetof(torque_model_s, m_torqueRequestedLimited) == 4);
static_assert(offsetof(torque_model_s, m_torqueLoss) == 6);
static_assert(offsetof(torque_model_s, m_torqueLossLoadAxisValue) == 8);
static_assert(offsetof(torque_model_s, m_grossTorque) == 10);
static_assert(offsetof(torque_model_s, m_airmassTarget) == 12);
static_assert(offsetof(torque_model_s, m_airmassActual) == 14);
static_assert(offsetof(torque_model_s, m_airmassTrim) == 16);
static_assert(offsetof(torque_model_s, m_throttleRequest) == 18);
static_assert(offsetof(torque_model_s, m_limiterXAxisValue) == 20);
static_assert(offsetof(torque_model_s, m_limiterYAxisValue) == 28);
static_assert(offsetof(torque_model_s, m_limiterTorque) == 36);
