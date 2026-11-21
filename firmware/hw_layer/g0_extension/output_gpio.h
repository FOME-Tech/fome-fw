#pragma once

struct hardware_pwm;
namespace g0 {
void registerOutputs();
bool isOutput(brain_pin_e pin);
hardware_pwm* startPwm(brain_pin_e pin, float frequency, float duty, bool inverted);
} // namespace g0
