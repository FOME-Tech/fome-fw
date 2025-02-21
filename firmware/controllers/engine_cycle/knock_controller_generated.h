#pragma once
#include "rusefi_types.h"
struct knock_controller_s {
	// Knock: Detected recently
	// offset 0 bit 0
	bool hasKnockRecently : 1 {};
	// Knock: Retard active
	// offset 0 bit 1
	bool hasKnockRetardNow : 1 {};
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
	// Knock: Current level
	// Volts
	// offset 4
	float m_knockLevel = (float)0;
	// Knock: Cyl
	// dBv
	// offset 8
	int8_t m_knockCyl[12];
	// Knock: Retard
	// deg
	// offset 20
	angle_t m_knockRetard = (angle_t)0;
	// Knock: Threshold
	// offset 24
	float m_knockThreshold = (float)0;
	// Knock: Count
	// offset 28
	uint32_t m_knockCount = (uint32_t)0;
	// Knock: Max retard
	// offset 32
	float m_maximumRetard = (float)0;
};
static_assert(sizeof(knock_controller_s) == 36);
static_assert(offsetof(knock_controller_s, m_knockLevel) == 4);
static_assert(offsetof(knock_controller_s, m_knockCyl) == 8);
static_assert(offsetof(knock_controller_s, m_knockRetard) == 20);
static_assert(offsetof(knock_controller_s, m_knockThreshold) == 24);
static_assert(offsetof(knock_controller_s, m_knockCount) == 28);
static_assert(offsetof(knock_controller_s, m_maximumRetard) == 32);

