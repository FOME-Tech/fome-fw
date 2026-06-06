#pragma once
#include "rusefi_types.h"
struct traction_control_state_s {
	// Measured wheel slip
	// %
	// offset 0
	scaled_channel<int16_t, 10, 1> slipMeasured = (int16_t)0;
	// Driven axle speed
	// kph
	// offset 2
	scaled_channel<uint16_t, 10, 1> drivenSpeed = (uint16_t)0;
	// Reference (undriven) axle speed
	// kph
	// offset 4
	scaled_channel<uint16_t, 10, 1> referenceSpeed = (uint16_t)0;
	// Target wheel slip
	// %
	// offset 6
	scaled_channel<uint8_t, 2, 1> slipTarget = (uint8_t)0;
	// offset 7
	uint8_t alignmentFill_at_7[1];
	// Slip target trim axis value
	// offset 8
	int16_t slipTargetYAxisValue = (int16_t)0;
	// Permitted axle torque
	// Nm
	// offset 10
	uint16_t axleTorqueLimit = (uint16_t)0;
	// Permitted engine torque
	// Nm
	// offset 12
	scaled_channel<int16_t, 10, 1> engineTorqueLimit = (int16_t)0;
	// Slip speed rate of change
	// kph/s
	// offset 14
	int16_t slipRate = (int16_t)0;
};
static_assert(sizeof(traction_control_state_s) == 16);
static_assert(offsetof(traction_control_state_s, slipMeasured) == 0);
static_assert(offsetof(traction_control_state_s, drivenSpeed) == 2);
static_assert(offsetof(traction_control_state_s, referenceSpeed) == 4);
static_assert(offsetof(traction_control_state_s, slipTarget) == 6);
static_assert(offsetof(traction_control_state_s, slipTargetYAxisValue) == 8);
static_assert(offsetof(traction_control_state_s, axleTorqueLimit) == 10);
static_assert(offsetof(traction_control_state_s, engineTorqueLimit) == 12);
static_assert(offsetof(traction_control_state_s, slipRate) == 14);
