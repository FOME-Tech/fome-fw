#pragma once
#include "rusefi_types.h"
struct trigger_central_s {
	// Hardware events since boot
	// offset 0
	uint16_t hwEventCounters[4];
	// offset 8
	uint16_t vvtCamCounter = (uint16_t)0;
	// offset 10
	uint8_t alignmentFill_at_10[2];
	// offset 12
	float mapVvt_MAP_AT_SPECIAL_POINT = (float)0;
	// offset 16
	float mapVvt_MAP_AT_DIFF = (float)0;
	// offset 20
	uint8_t mapVvt_MAP_AT_CYCLE_COUNT = (uint8_t)0;
	// offset 21
	uint8_t mapVvt_map_peak = (uint8_t)0;
	// offset 22
	uint8_t alignmentFill_at_22[2];
	// Engine Phase
	// deg
	// offset 24
	float currentEngineDecodedPhase = (float)0;
	// deg
	// offset 28
	float triggerToothAngleError = (float)0;
	// offset 32
	uint8_t triggerIgnoredToothCount = (uint8_t)0;
	// offset 33
	uint8_t alignmentFill_at_33[3];
};
static_assert(sizeof(trigger_central_s) == 36);
static_assert(offsetof(trigger_central_s, hwEventCounters) == 0);
static_assert(offsetof(trigger_central_s, vvtCamCounter) == 8);
static_assert(offsetof(trigger_central_s, mapVvt_MAP_AT_SPECIAL_POINT) == 12);
static_assert(offsetof(trigger_central_s, mapVvt_MAP_AT_DIFF) == 16);
static_assert(offsetof(trigger_central_s, mapVvt_MAP_AT_CYCLE_COUNT) == 20);
static_assert(offsetof(trigger_central_s, mapVvt_map_peak) == 21);
static_assert(offsetof(trigger_central_s, currentEngineDecodedPhase) == 24);
static_assert(offsetof(trigger_central_s, triggerToothAngleError) == 28);
static_assert(offsetof(trigger_central_s, triggerIgnoredToothCount) == 32);

