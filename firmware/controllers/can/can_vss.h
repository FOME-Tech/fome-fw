/**
 * @file	can_vss.h
 *
 * @date Apr 19, 2020
 * @author Alex Miculescu, (c) 2020
 */

#pragma once

struct engine_configuration_s;

// Configuration sequence step, see config_steps.cpp
void stopCanVss(const engine_configuration_s* prev);
void startCanVss(const engine_configuration_s* prev);

void setCanVss(int type);

#if EFI_CAN_SUPPORT
void processCanRxVss(const CANRxFrame& frame, efitick_t nowNt);

// Brake pedal switch state, if the selected vehicle's CAN bus provides one
expected<bool> getCanBrakePedalState();
#endif
