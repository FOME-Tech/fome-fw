#pragma once
#include "rusefi_types.h"
struct trigger_central_s {
	// offset 0
	float mapVvt_MAP_AT_SPECIAL_POINT = (float)0;
	// offset 4
	float mapVvt_MAP_AT_DIFF = (float)0;
	// offset 8
	uint8_t mapVvt_MAP_AT_CYCLE_COUNT = (uint8_t)0;
	// offset 9
	uint8_t mapVvt_map_peak = (uint8_t)0;
	// Trg: Ignored tooth count
	// offset 10
	uint8_t triggerIgnoredToothCount = (uint8_t)0;
	// offset 11
	uint8_t alignmentFill_at_11[1];
	// Engine Phase
	// deg
	// offset 12
	float currentEngineDecodedPhase = (float)0;
	// Trg: Tooth angle error
	// deg
	// offset 16
	float triggerToothAngleError = (float)0;
};
static_assert(sizeof(trigger_central_s) == 20);
static_assert(offsetof(trigger_central_s, mapVvt_MAP_AT_SPECIAL_POINT) == 0);
static_assert(offsetof(trigger_central_s, mapVvt_MAP_AT_DIFF) == 4);
static_assert(offsetof(trigger_central_s, mapVvt_MAP_AT_CYCLE_COUNT) == 8);
static_assert(offsetof(trigger_central_s, mapVvt_map_peak) == 9);
static_assert(offsetof(trigger_central_s, triggerIgnoredToothCount) == 10);
static_assert(offsetof(trigger_central_s, currentEngineDecodedPhase) == 12);
static_assert(offsetof(trigger_central_s, triggerToothAngleError) == 16);

