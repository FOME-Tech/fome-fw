#pragma once

struct engine_configuration_s;

// Configuration sequence step, see config_steps.cpp
void stopGpPwm(const engine_configuration_s* prev);
void startGpPwm(const engine_configuration_s* prev);

void updateGppwm();
