#pragma once
#include "rusefi_types.h"
struct torque_model_s {
	// Driver requested torque
	// Nm
	// offset 0
	scaled_channel<int16_t, 10, 1> m_driverTorqueDemand = (int16_t)0;
	// Idle requested torque
	// Nm
	// offset 2
	scaled_channel<int16_t, 100, 1> m_idleTorqueDemand = (int16_t)0;
	// Requested torque
	// Nm
	// offset 4
	scaled_channel<int16_t, 10, 1> m_torqueRequested = (int16_t)0;
	// Requested torque (limited)
	// Nm
	// offset 6
	scaled_channel<int16_t, 10, 1> m_torqueRequestedLimited = (int16_t)0;
	// Torque loss
	// Nm
	// offset 8
	scaled_channel<int16_t, 10, 1> m_torqueLoss = (int16_t)0;
	// Loss table Y axis value
	// offset 10
	int16_t m_torqueLossLoadAxisValue = (int16_t)0;
	// Gross torque
	// Nm
	// offset 12
	scaled_channel<int16_t, 10, 1> m_grossTorque = (int16_t)0;
	// Target cycle airmass
	// g
	// offset 14
	scaled_channel<uint16_t, 1000, 1> m_airmassTarget = (uint16_t)0;
	// Measured cycle airmass
	// g
	// offset 16
	scaled_channel<uint16_t, 1000, 1> m_airmassActual = (uint16_t)0;
	// Target airmass CL trim
	// %
	// offset 18
	scaled_channel<int16_t, 100, 1> m_airmassTrim = (int16_t)0;
	// Requested throttle position
	// %
	// offset 20
	scaled_channel<uint16_t, 100, 1> m_throttleRequest = (uint16_t)0;
	// Spark torque reduction request
	// %
	// offset 22
	scaled_channel<uint8_t, 2, 1> m_sparkReductionRequest = (uint8_t)0;
	// offset 23
	uint8_t alignmentFill_at_23[1];
	// Generic limiter X axis
	// offset 24
	int16_t m_limiterXAxisValue[4];
	// Generic limiter Y axis
	// offset 32
	int16_t m_limiterYAxisValue[4];
	// Generic limiter ceiling
	// Nm
	// offset 40
	uint16_t m_limiterTorque[4];
	// Limited by engine max
	// offset 48 bit 0
	bool limitedByEngineMax : 1 {};
	// Limited by axle max
	// offset 48 bit 1
	bool limitedByAxleMax : 1 {};
	// Limited by traction control
	// offset 48 bit 2
	bool limitedByTraction : 1 {};
	// Limited by generic limiter 1
	// offset 48 bit 3
	bool limitedByGenericLimiter1 : 1 {};
	// Limited by generic limiter 2
	// offset 48 bit 4
	bool limitedByGenericLimiter2 : 1 {};
	// Limited by generic limiter 3
	// offset 48 bit 5
	bool limitedByGenericLimiter3 : 1 {};
	// Limited by generic limiter 4
	// offset 48 bit 6
	bool limitedByGenericLimiter4 : 1 {};
	// offset 48 bit 7
	bool unusedBit_48_7 : 1 {};
	// offset 48 bit 8
	bool unusedBit_48_8 : 1 {};
	// offset 48 bit 9
	bool unusedBit_48_9 : 1 {};
	// offset 48 bit 10
	bool unusedBit_48_10 : 1 {};
	// offset 48 bit 11
	bool unusedBit_48_11 : 1 {};
	// offset 48 bit 12
	bool unusedBit_48_12 : 1 {};
	// offset 48 bit 13
	bool unusedBit_48_13 : 1 {};
	// offset 48 bit 14
	bool unusedBit_48_14 : 1 {};
	// offset 48 bit 15
	bool unusedBit_48_15 : 1 {};
	// offset 48 bit 16
	bool unusedBit_48_16 : 1 {};
	// offset 48 bit 17
	bool unusedBit_48_17 : 1 {};
	// offset 48 bit 18
	bool unusedBit_48_18 : 1 {};
	// offset 48 bit 19
	bool unusedBit_48_19 : 1 {};
	// offset 48 bit 20
	bool unusedBit_48_20 : 1 {};
	// offset 48 bit 21
	bool unusedBit_48_21 : 1 {};
	// offset 48 bit 22
	bool unusedBit_48_22 : 1 {};
	// offset 48 bit 23
	bool unusedBit_48_23 : 1 {};
	// offset 48 bit 24
	bool unusedBit_48_24 : 1 {};
	// offset 48 bit 25
	bool unusedBit_48_25 : 1 {};
	// offset 48 bit 26
	bool unusedBit_48_26 : 1 {};
	// offset 48 bit 27
	bool unusedBit_48_27 : 1 {};
	// offset 48 bit 28
	bool unusedBit_48_28 : 1 {};
	// offset 48 bit 29
	bool unusedBit_48_29 : 1 {};
	// offset 48 bit 30
	bool unusedBit_48_30 : 1 {};
	// offset 48 bit 31
	bool unusedBit_48_31 : 1 {};
};
static_assert(sizeof(torque_model_s) == 52);
static_assert(offsetof(torque_model_s, m_driverTorqueDemand) == 0);
static_assert(offsetof(torque_model_s, m_idleTorqueDemand) == 2);
static_assert(offsetof(torque_model_s, m_torqueRequested) == 4);
static_assert(offsetof(torque_model_s, m_torqueRequestedLimited) == 6);
static_assert(offsetof(torque_model_s, m_torqueLoss) == 8);
static_assert(offsetof(torque_model_s, m_torqueLossLoadAxisValue) == 10);
static_assert(offsetof(torque_model_s, m_grossTorque) == 12);
static_assert(offsetof(torque_model_s, m_airmassTarget) == 14);
static_assert(offsetof(torque_model_s, m_airmassActual) == 16);
static_assert(offsetof(torque_model_s, m_airmassTrim) == 18);
static_assert(offsetof(torque_model_s, m_throttleRequest) == 20);
static_assert(offsetof(torque_model_s, m_sparkReductionRequest) == 22);
static_assert(offsetof(torque_model_s, m_limiterXAxisValue) == 24);
static_assert(offsetof(torque_model_s, m_limiterYAxisValue) == 32);
static_assert(offsetof(torque_model_s, m_limiterTorque) == 40);
