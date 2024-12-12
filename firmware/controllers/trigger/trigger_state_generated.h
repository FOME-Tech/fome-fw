#pragma once
#include "rusefi_types.h"
struct trigger_state_s {
	// Crank sync counter
	// Usually matches crank revolutions
	// offset 0
	uint32_t crankSynchronizationCounter = (uint32_t)0;
	// Trigger Sync Latest Ratio
	// offset 4
	float triggerSyncGapRatio = (float)0;
	// offset 8
	uint8_t triggerStateIndex = (uint8_t)0;
	// offset 9
	uint8_t vvtCounter = (uint8_t)0;
	// offset 10
	uint8_t vvtStateIndex = (uint8_t)0;
	// offset 11
	uint8_t alignmentFill_at_11[1];
};
static_assert(sizeof(trigger_state_s) == 12);
static_assert(offsetof(trigger_state_s, crankSynchronizationCounter) == 0);
static_assert(offsetof(trigger_state_s, triggerSyncGapRatio) == 4);
static_assert(offsetof(trigger_state_s, triggerStateIndex) == 8);
static_assert(offsetof(trigger_state_s, vvtCounter) == 9);
static_assert(offsetof(trigger_state_s, vvtStateIndex) == 10);

