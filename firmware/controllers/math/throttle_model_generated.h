#pragma once
#include "rusefi_types.h"
struct throttle_model_s {
	// Use WOT model
	// offset 0 bit 0
	bool m_useWotModel : 1 {};
	// offset 0 bit 1
	bool unusedBit_0_1 : 1 {};
	// offset 0 bit 2
	bool unusedBit_0_2 : 1 {};
	// offset 0 bit 3
	bool unusedBit_0_3 : 1 {};
	// offset 0 bit 4
	bool unusedBit_0_4 : 1 {};
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
	// Crossover TPS
	// %
	// offset 4
	scaled_channel<int16_t, 100, 1> m_crossoverAngle = (int16_t)0;
	// offset 6
	uint8_t alignmentFill_at_6[2];
	// Flow estimate
	// g/s
	// offset 8
	float m_flowEstimate = (float)0;
};
static_assert(sizeof(throttle_model_s) == 12);
static_assert(offsetof(throttle_model_s, m_crossoverAngle) == 4);
static_assert(offsetof(throttle_model_s, m_flowEstimate) == 8);

