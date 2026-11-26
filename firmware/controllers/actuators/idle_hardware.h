/**
 * @file    idle_hardware.h
 * @brief   Idle Air Control valve hardware
 *
 * @date November 3, 2020
 *
 * This is just the hardware interface - deciding where to put the valve happens in idle_thread.cpp
 */

#pragma once

struct engine_configuration_s;

// Configuration sequence step, see config_steps.cpp: the idle valve (solenoid or stepper) hardware
void stopIdleHardware(const engine_configuration_s* prev);
void startIdleHardware(const engine_configuration_s* prev);

bool isIdleMotorBusy();
