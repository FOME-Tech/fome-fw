/*
 * @file digital_input_exti.h
 *
 *  Created on: Dec 18, 2018
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#pragma once

#if HAL_USE_PAL

using ExtiCallback = void (*)(void*, efitick_t, bool);

struct ExtiEdgeCounter {
	// Free-running count of edges seen on the pin, wraps around
	uint32_t Count;
	// getTimeNowLowerNt() at the most recent edge
	uint32_t LastEdgeTimestamp;
};

void efiExtiInit();
// cb may be null: the pin then only counts edges, see efiExtiGetCounter
void efiExtiEnablePin(const char* msg, brain_pin_e pin, uint32_t mode, ExtiCallback cb, void* cb_data);
void efiExtiDisablePin(brain_pin_e brainPin);
// Consistent snapshot of a pin's edge counter, or all zero if that pin isn't enabled.
// Call from thread context only: interrupts are briefly masked, then unconditionally unmasked.
ExtiEdgeCounter efiExtiGetCounter(brain_pin_e brainPin);
uint8_t getExtiOverflowCounter();
#endif /* HAL_USE_PAL */
