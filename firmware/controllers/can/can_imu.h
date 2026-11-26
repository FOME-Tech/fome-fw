#pragma once

#include "can.h"

// Configuration sequence step, see config_steps.cpp
void stopCanImu(const engine_configuration_s* prev);
void startCanImu(const engine_configuration_s* prev);
void processCanRxImu(const CANRxFrame& frame);
