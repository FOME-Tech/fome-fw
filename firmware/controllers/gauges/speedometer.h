#pragma once

struct engine_configuration_s;

// Configuration sequence step, see config_steps.cpp
void stopSpeedometer(const engine_configuration_s* prev);
void startSpeedometer(const engine_configuration_s* prev);

void speedoUpdate();
