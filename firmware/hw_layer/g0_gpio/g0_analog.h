#pragma once

#include "pch.h"
#include "adc_provider.h"

void startG070SpiAdcProvider();
bool readG070DigitalInput(size_t idx);
bool isG070LowsidePin(brain_pin_e pin);
void setG070LowsideOutput(brain_pin_e pin, bool value);
void disableG070LowsideOutput(brain_pin_e pin);

#ifdef __cplusplus
struct hardware_pwm;
hardware_pwm* tryInitG070LowsidePwm(brain_pin_e pin, float frequencyHz, float duty);
#endif
