/**
 * @file	io_pins.h
 * @brief	this file is about general input/output utility methods, not much EFI-specifics
 *
 * @date Jan 24, 2013
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#pragma once

#include "global.h"

#define INITIAL_PIN_STATE -1
#define GPIO_NULL NULL

// mode >= 0  is always true since that's an unsigned
#define assertOMode(mode) { \
	efiAssertVoid(ObdCode::CUSTOM_INVALID_MODE_SETTING, mode <= OM_OPENDRAIN_INVERTED, "invalid pin_output_mode_e"); \
 }

#define efiSetPadModeIfConfigurationChanged(msg, pin, mode)                      \
	if (isConfigurationChanged(pin)) {                                           \
		efiSetPadMode(msg, engineConfiguration->pin, mode);      \
	}

#define efiSetPadUnusedIfConfigurationChanged(pin)                               \
	if (isConfigurationChanged(pin)) {                                           \
		efiSetPadUnused(activeConfiguration.pin);   \
	}

EXTERNC void efiSetPadMode(const char *msg, brain_pin_e pin, iomode_t mode);
EXTERNC void efiSetPadModeWithoutOwnershipAcquisition(const char *msg, brain_pin_e brainPin, iomode_t mode);
EXTERNC void efiSetPadUnused(brain_pin_e brainPin);

#if EFI_GPIO_HARDWARE
EXTERNC bool efiReadPin(brain_pin_e pin);

EXTERNC iomode_t getInputMode(pin_input_mode_e mode);

#endif /* EFI_GPIO_HARDWARE */

void writePad(const char *msg, brain_pin_e pin, int bit);

#if ! EFI_PROD_CODE
#define BRAIN_PIN_COUNT (1 << 8 * sizeof(brain_pin_e))
extern bool mockPinStates[BRAIN_PIN_COUNT];

void setMockState(brain_pin_e pin, bool state);
#endif
