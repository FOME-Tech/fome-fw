#pragma once
#include "rusefi_types.h"
struct trigger_state_s {
	// Sync counter
	// offset 0
	uint32_t crankSynchronizationCounter = (uint32_t)0;
	// Sync gap ratio
	// offset 4
	float triggerSyncGapRatio = (float)0;
	// Trigger state index
	// offset 8
	uint8_t triggerStateIndex = (uint8_t)0;
	// offset 9
	uint8_t alignmentFill_at_9[3];
};
static_assert(sizeof(trigger_state_s) == 12);
static_assert(offsetof(trigger_state_s, crankSynchronizationCounter) == 0);
static_assert(offsetof(trigger_state_s, triggerSyncGapRatio) == 4);
static_assert(offsetof(trigger_state_s, triggerStateIndex) == 8);

