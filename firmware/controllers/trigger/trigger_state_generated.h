#pragma once
#include "rusefi_types.h"
struct trigger_state_s {
	// Sync gap ratio
	// offset 0
	float triggerSyncGapRatio = (float)0;
	// Sync counter
	// offset 4
	uint16_t crankSynchronizationCounter = (uint16_t)0;
	// Trigger state index
	// offset 6
	uint8_t triggerStateIndex = (uint8_t)0;
	// offset 7
	uint8_t alignmentFill_at_7[1];
	// Edge count rise
	// offset 8
	uint16_t edgeCountRise = (uint16_t)0;
	// Edge count fall
	// offset 10
	uint16_t edgeCountFall = (uint16_t)0;
};
static_assert(sizeof(trigger_state_s) == 12);
static_assert(offsetof(trigger_state_s, triggerSyncGapRatio) == 0);
static_assert(offsetof(trigger_state_s, crankSynchronizationCounter) == 4);
static_assert(offsetof(trigger_state_s, triggerStateIndex) == 6);
static_assert(offsetof(trigger_state_s, edgeCountRise) == 8);
static_assert(offsetof(trigger_state_s, edgeCountFall) == 10);

