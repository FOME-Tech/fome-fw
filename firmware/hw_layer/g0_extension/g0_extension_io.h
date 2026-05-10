#pragma once

#include "pch.h"
#include "adc_provider.h"

void startG0ExtensionIo();
bool readG0ExtensionDigitalInput(size_t idx);
bool isG0ExtensionLowsidePin(brain_pin_e pin);
void setG0ExtensionLowsideOutput(brain_pin_e pin, bool value);
void disableG0ExtensionLowsideOutput(brain_pin_e pin);

#ifdef __cplusplus
struct hardware_pwm;
hardware_pwm* tryInitG0ExtensionLowsidePwm(brain_pin_e pin, float frequencyHz, float duty);
#endif
