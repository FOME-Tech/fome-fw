#pragma once
#include "rusefi_types.h"
struct electronic_throttle_s {
	// DC: wastegatePosition
	// %
	// offset 0
	float m_wastegatePosition = (float)0;
	// Target: Base
	// %
	// offset 4
	scaled_channel<int16_t, 100, 1> m_baseTarget = (int16_t)0;
	// Target: Trim
	// %
	// offset 6
	scaled_channel<int16_t, 100, 1> m_trim = (int16_t)0;
	// Target: Lua adjustment
	// %
	// offset 8
	float luaAdjustment = (float)0;
	// Target
	// %
	// offset 12
	scaled_channel<int16_t, 100, 1> m_adjustedTarget = (int16_t)0;
	// Feed forward
	// %
	// offset 14
	scaled_channel<int16_t, 100, 1> m_feedForward = (int16_t)0;
	// Error
	// %
	// offset 16
	scaled_channel<int16_t, 100, 1> m_error = (int16_t)0;
	// Duty cycle
	// %
	// offset 18
	scaled_channel<uint8_t, 2, 1> m_outputDuty = (uint8_t)0;
	// offset 19
	uint8_t alignmentFill_at_19[1];
	// Rev limit active
	// offset 20 bit 0
	bool revLimitActive : 1 {};
	// Jam detected
	// offset 20 bit 1
	bool jamDetected : 1 {};
	// offset 20 bit 2
	bool unusedBit_20_2 : 1 {};
	// offset 20 bit 3
	bool unusedBit_20_3 : 1 {};
	// offset 20 bit 4
	bool unusedBit_20_4 : 1 {};
	// offset 20 bit 5
	bool unusedBit_20_5 : 1 {};
	// offset 20 bit 6
	bool unusedBit_20_6 : 1 {};
	// offset 20 bit 7
	bool unusedBit_20_7 : 1 {};
	// offset 20 bit 8
	bool unusedBit_20_8 : 1 {};
	// offset 20 bit 9
	bool unusedBit_20_9 : 1 {};
	// offset 20 bit 10
	bool unusedBit_20_10 : 1 {};
	// offset 20 bit 11
	bool unusedBit_20_11 : 1 {};
	// offset 20 bit 12
	bool unusedBit_20_12 : 1 {};
	// offset 20 bit 13
	bool unusedBit_20_13 : 1 {};
	// offset 20 bit 14
	bool unusedBit_20_14 : 1 {};
	// offset 20 bit 15
	bool unusedBit_20_15 : 1 {};
	// offset 20 bit 16
	bool unusedBit_20_16 : 1 {};
	// offset 20 bit 17
	bool unusedBit_20_17 : 1 {};
	// offset 20 bit 18
	bool unusedBit_20_18 : 1 {};
	// offset 20 bit 19
	bool unusedBit_20_19 : 1 {};
	// offset 20 bit 20
	bool unusedBit_20_20 : 1 {};
	// offset 20 bit 21
	bool unusedBit_20_21 : 1 {};
	// offset 20 bit 22
	bool unusedBit_20_22 : 1 {};
	// offset 20 bit 23
	bool unusedBit_20_23 : 1 {};
	// offset 20 bit 24
	bool unusedBit_20_24 : 1 {};
	// offset 20 bit 25
	bool unusedBit_20_25 : 1 {};
	// offset 20 bit 26
	bool unusedBit_20_26 : 1 {};
	// offset 20 bit 27
	bool unusedBit_20_27 : 1 {};
	// offset 20 bit 28
	bool unusedBit_20_28 : 1 {};
	// offset 20 bit 29
	bool unusedBit_20_29 : 1 {};
	// offset 20 bit 30
	bool unusedBit_20_30 : 1 {};
	// offset 20 bit 31
	bool unusedBit_20_31 : 1 {};
	// TPS error counter
	// count
	// offset 24
	uint16_t etbTpsErrorCounter = (uint16_t)0;
	// Pedal error counter
	// count
	// offset 26
	uint16_t etbPpsErrorCounter = (uint16_t)0;
	// Jam timer
	// sec
	// offset 28
	scaled_channel<uint16_t, 100, 1> jamTimer = (uint16_t)0;
	// Error code
	// offset 30
	int8_t etbErrorCode = (int8_t)0;
	// offset 31
	uint8_t alignmentFill_at_31[1];
};
static_assert(sizeof(electronic_throttle_s) == 32);
static_assert(offsetof(electronic_throttle_s, m_wastegatePosition) == 0);
static_assert(offsetof(electronic_throttle_s, m_baseTarget) == 4);
static_assert(offsetof(electronic_throttle_s, m_trim) == 6);
static_assert(offsetof(electronic_throttle_s, luaAdjustment) == 8);
static_assert(offsetof(electronic_throttle_s, m_adjustedTarget) == 12);
static_assert(offsetof(electronic_throttle_s, m_feedForward) == 14);
static_assert(offsetof(electronic_throttle_s, m_error) == 16);
static_assert(offsetof(electronic_throttle_s, m_outputDuty) == 18);
static_assert(offsetof(electronic_throttle_s, etbTpsErrorCounter) == 24);
static_assert(offsetof(electronic_throttle_s, etbPpsErrorCounter) == 26);
static_assert(offsetof(electronic_throttle_s, jamTimer) == 28);
static_assert(offsetof(electronic_throttle_s, etbErrorCode) == 30);

