#pragma once

#include <cstdint>
#include "hal.h"

// Counts since boot, wrapping modulo 2^32 (about 5 days at 10 kHz). No runtime reset:
// each field has one serialized writer, and console readers use a locked snapshot.
struct AdcDiagnostics {
	uint32_t started = 0;
	uint32_t completed = 0;
	uint32_t errors = 0;
	uint32_t lastError = 0;
	uint32_t skippedActive = 0;
	uint32_t skippedComplete = 0;
	uint32_t skippedNotReady = 0;
	uint32_t skippedNoChannels = 0;
	uint32_t skippedInvalidChannels = 0;
	uint32_t skippedDisabled = 0;
	uint32_t skippedPending = 0;
	uint32_t processed = 0;
};

extern AdcDiagnostics fastAdcDiagnostics;
extern AdcDiagnostics knockAdcDiagnostics;

// Called only after the caller's existing readiness guard has rejected a start.
inline void recordAdcSkippedState(AdcDiagnostics& counters, adcstate_t state) {
	if (state == ADC_ACTIVE) {
		counters.skippedActive++;
	} else if (state == ADC_COMPLETE) {
		counters.skippedComplete++;
	} else {
		counters.skippedNotReady++;
	}
}

struct AdcDiagnosticsSnapshot {
	AdcDiagnostics fast;
	AdcDiagnostics knock;
};

AdcDiagnosticsSnapshot getAdcDiagnostics();
void initAdcDiagnostics(bool fastIsCircular);
