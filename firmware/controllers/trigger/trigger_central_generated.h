#pragma once
#include "rusefi_types.h"
struct trigger_central_s {
	// kPa
	// offset 0
	scaled_channel<uint16_t, 30, 1> mapVvt_MAP_AT_SPECIAL_POINT = (uint16_t)0;
	// kPa
	// offset 2
	scaled_channel<int16_t, 30, 1> mapVvt_MAP_AT_DIFF = (int16_t)0;
	// Engine Phase
	// deg
	// offset 4
	scaled_channel<uint16_t, 10, 1> currentEngineDecodedPhase = (uint16_t)0;
	// Trg: Tooth angle error
	// deg
	// offset 6
	int16_t triggerToothAngleError = (int16_t)0;
	// Trg: Ignored tooth count
	// offset 8
	uint8_t triggerIgnoredToothCount = (uint8_t)0;
	// offset 9
	uint8_t alignmentFill_at_9[3];
};
static_assert(sizeof(trigger_central_s) == 12);
static_assert(offsetof(trigger_central_s, mapVvt_MAP_AT_SPECIAL_POINT) == 0);
static_assert(offsetof(trigger_central_s, mapVvt_MAP_AT_DIFF) == 2);
static_assert(offsetof(trigger_central_s, currentEngineDecodedPhase) == 4);
static_assert(offsetof(trigger_central_s, triggerToothAngleError) == 6);
static_assert(offsetof(trigger_central_s, triggerIgnoredToothCount) == 8);
