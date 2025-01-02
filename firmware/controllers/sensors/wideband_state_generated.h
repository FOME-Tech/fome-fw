#pragma once
#include "rusefi_types.h"
struct wideband_state_s {
	// Fault code
	// offset 0
	uint8_t faultCode = (uint8_t)0;
	// Heater duty
	// %
	// offset 1
	uint8_t heaterDuty = (uint8_t)0;
	// Pump duty
	// %
	// offset 2
	uint8_t pumpDuty = (uint8_t)0;
	// offset 3
	uint8_t alignmentFill_at_3[1];
	// Temperature
	// C
	// offset 4
	uint16_t tempC = (uint16_t)0;
	// Nernst Voltage
	// V
	// offset 6
	scaled_channel<uint16_t, 1000, 1> nernstVoltage = (uint16_t)0;
	// ESR
	// ohm
	// offset 8
	uint16_t esr = (uint16_t)0;
	// offset 10
	uint8_t alignmentFill_at_10[2];
};
static_assert(sizeof(wideband_state_s) == 12);
static_assert(offsetof(wideband_state_s, faultCode) == 0);
static_assert(offsetof(wideband_state_s, heaterDuty) == 1);
static_assert(offsetof(wideband_state_s, pumpDuty) == 2);
static_assert(offsetof(wideband_state_s, tempC) == 4);
static_assert(offsetof(wideband_state_s, nernstVoltage) == 6);
static_assert(offsetof(wideband_state_s, esr) == 8);

