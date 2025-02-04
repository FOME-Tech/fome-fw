/**
 * @file	can_tx.cpp
 *
 * CAN transmission handling.  This file handles the dispatch of various outgoing regularly scheduled CAN message types.
 *
 * @date Mar 19, 2020
 * @author Matthew Kennedy, (c) 2020
 */

#include "pch.h"

#if EFI_CAN_SUPPORT
#include "can.h"
#include "can_hw.h"
#include "can_dash.h"
#include "obd2.h"
#include "can_sensor.h"
#include "rusefi_wideband.h"

extern CanListener* canListeners_head;


CanWrite::CanWrite()
	: PeriodicController("CAN TX", PRIO_CAN_TX, CAN_CYCLE_FREQ)
{
}

static CI roundTxPeriodToCycle(uint16_t period) {
	if (period < 10) return CI::_5ms;
	else if (period < 20) return CI::_10ms;
	else if (period < 50) return CI::_20ms;
	else if (period < 100) return CI::_50ms;
	else if (period < 200) return CI::_100ms;
	else if (period < 250) return CI::_200ms;
	else if (period < 500) return CI::_250ms;
	else if (period < 1000) return CI::_500ms;
	else return CI::_1000ms;
}

void CanWrite::PeriodicTask(efitick_t) {
	static uint16_t cycleCount = 0;
	CanCycle cycle(cycleCount);

	//in case we have Verbose Can enabled, we should keep user configured period
	if (engineConfiguration->enableVerboseCanTx) {
		auto roundedInterval = roundTxPeriodToCycle(engineConfiguration->canSleepPeriodMs);
		if (cycle.isInterval(roundedInterval)) {
			void sendCanVerbose();
			sendCanVerbose();
		}
	}

	CanListener* current = canListeners_head;

	while (current) {
		current = current->request();
	}

	if (cycle.isInterval(CI::_MAX_Cycle)) {
		//we now reset cycleCount since we reached max cycle count
		cycleCount = 0;
	}

	updateDash(cycle);

#if EFI_WIDEBAND_FIRMWARE_UPDATE
	if (engineConfiguration->enableAemXSeries && cycle.isInterval(CI::_50ms)) {
		sendWidebandInfo();
	}
#endif

	cycleCount++;
}

CanInterval CanCycle::computeFlags(uint32_t cycleCount) {
	// These numbers only work at 200hz!
	static_assert(CAN_CYCLE_FREQ == 200);

	CanInterval cycleMask = CanInterval::_5ms;

	if ((cycleCount % 2) == 0) {
		cycleMask |= CI::_10ms;
	}

	if ((cycleCount % 4) == 0) {
		cycleMask |= CI::_20ms;
	}

	if ((cycleCount % 10) == 0) {
		cycleMask |= CI::_50ms;
	}

	if ((cycleCount % 20) == 0) {
		cycleMask |= CI::_100ms;
	}

	if ((cycleCount % 40) == 0) {
		cycleMask |= CI::_200ms;
	}

	if ((cycleCount % 50) == 0) {
		cycleMask |= CI::_250ms;
	}

	if ((cycleCount % 100) == 0) {
		cycleMask |= CI::_500ms;
	}

	if ((cycleCount % 200) == 0) {
		cycleMask |= CI::_1000ms;
	}

	return cycleMask;
}

#endif // EFI_CAN_SUPPORT
