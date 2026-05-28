/*
 * @file cpu_usage.cpp
 *
 * Aggregates idle/ISR time accounting from the ChibiOS hooks into a
 * rolling CPU-usage percentage.
 *
 * Idle time is credited only while the idle thread is running and no ISR
 * is preempting it; ISRs that fire during idle (or any time outside idle)
 * count as busy CPU. The result is sampled by cpuUsageSampleAndReset()
 * at a regular cadence (~2 Hz from the slow callback).
 */

#include "pch.h"
#include "cpu_usage.h"

#if !EFI_UNIT_TEST

static volatile bool s_inIdle = false;
static volatile uint8_t s_isrNestDepth = 0;
static volatile uint32_t s_idleEnterTs = 0;
static volatile uint32_t s_idleAccumNt = 0;
static volatile uint32_t s_windowStartTs = 0;

void cpuUsageOnIdleEnter() {
	s_idleEnterTs = port_rt_get_counter_value();
	s_inIdle = true;
}

void cpuUsageOnIdleExit() {
	if (s_inIdle) {
		uint32_t now = port_rt_get_counter_value();
		s_idleAccumNt += (uint32_t)(now - s_idleEnterTs);
		s_inIdle = false;
	}
}

void cpuUsageOnIsrEnter() {
	if (s_inIdle && s_isrNestDepth == 0) {
		// Credit the idle span that ended at the start of this ISR, then
		// pause the idle timer until the (outermost) ISR returns.
		uint32_t now = port_rt_get_counter_value();
		s_idleAccumNt += (uint32_t)(now - s_idleEnterTs);
	}
	s_isrNestDepth++;
}

void cpuUsageOnIsrExit() {
	if (s_isrNestDepth > 0) {
		s_isrNestDepth--;
	}
	if (s_inIdle && s_isrNestDepth == 0) {
		// Resume the idle timer; the time spent in the ISR is not credited.
		s_idleEnterTs = port_rt_get_counter_value();
	}
}

uint8_t cpuUsageSampleAndReset() {
	uint32_t now = port_rt_get_counter_value();

	// Snapshot + reset the accumulator. If we happen to be in idle right now,
	// credit the partial span up to "now" so it isn't lost across the window
	// boundary.
	uint32_t idleNt = s_idleAccumNt;
	s_idleAccumNt = 0;

	if (s_inIdle) {
		uint32_t partial = (uint32_t)(now - s_idleEnterTs);
		idleNt += partial;
		s_idleEnterTs = now;
	}

	uint32_t windowNt = (uint32_t)(now - s_windowStartTs);
	s_windowStartTs = now;

	if (windowNt == 0) {
		return 0;
	}

	if (idleNt >= windowNt) {
		return 0;
	}

	uint32_t busyNt = windowNt - idleNt;
	uint32_t pct = (uint64_t)busyNt * 100 / windowNt;
	if (pct > 100) {
		pct = 100;
	}
	return (uint8_t)pct;
}

#else // EFI_UNIT_TEST

void cpuUsageOnIdleEnter() {}
void cpuUsageOnIdleExit() {}
void cpuUsageOnIsrEnter() {}
void cpuUsageOnIsrExit() {}
uint8_t cpuUsageSampleAndReset() { return 0; }

#endif // !EFI_UNIT_TEST
