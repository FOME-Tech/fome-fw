#pragma once
#include "rusefi_types.h"
struct trigger_central_s {
	// Hardware events since boot
	// offset 0
	uint16_t triggerEventCounter[4];
	// offset 8
	uint16_t vvtCamCounter[4];
	// offset 16
	float mapVvt_MAP_AT_SPECIAL_POINT = (float)0;
	// offset 20
	float mapVvt_MAP_AT_DIFF = (float)0;
	// offset 24
	uint8_t mapVvt_MAP_AT_CYCLE_COUNT = (uint8_t)0;
	// offset 25
	uint8_t mapVvt_map_peak = (uint8_t)0;
	// offset 26
	uint8_t alignmentFill_at_26[2];
	// Engine Phase
	// deg
	// offset 28
	float currentEngineDecodedPhase = (float)0;
	// deg
	// offset 32
	float triggerToothAngleError = (float)0;
	// offset 36
	uint8_t triggerIgnoredToothCount = (uint8_t)0;
	// offset 37
	uint8_t alignmentFill_at_37[3];
};
static_assert(sizeof(trigger_central_s) == 40);
static_assert(offsetof(trigger_central_s, triggerEventCounter) == 0);
static_assert(offsetof(trigger_central_s, vvtCamCounter) == 8);
static_assert(offsetof(trigger_central_s, mapVvt_MAP_AT_SPECIAL_POINT) == 16);
static_assert(offsetof(trigger_central_s, mapVvt_MAP_AT_DIFF) == 20);
static_assert(offsetof(trigger_central_s, mapVvt_MAP_AT_CYCLE_COUNT) == 24);
static_assert(offsetof(trigger_central_s, mapVvt_map_peak) == 25);
static_assert(offsetof(trigger_central_s, currentEngineDecodedPhase) == 28);
static_assert(offsetof(trigger_central_s, triggerToothAngleError) == 32);
static_assert(offsetof(trigger_central_s, triggerIgnoredToothCount) == 36);

