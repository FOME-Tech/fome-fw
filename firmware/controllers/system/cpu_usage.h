/*
 * @file cpu_usage.h
 *
 * Aggregates idle/ISR time accounting from the ChibiOS hooks into a
 * rolling CPU-usage percentage. Sample once per measurement window via
 * cpuUsageSampleAndReset().
 */

#pragma once

#include <cstdint>

// ChibiOS hook callbacks. Safe to call from ISR prologue/epilogue and from
// the idle thread's critical-section enter/leave hooks.
void cpuUsageOnIdleEnter();
void cpuUsageOnIdleExit();
void cpuUsageOnIsrEnter();
void cpuUsageOnIsrExit();

// Returns CPU usage % (0..100) since the last call, then opens a new window.
uint8_t cpuUsageSampleAndReset();
