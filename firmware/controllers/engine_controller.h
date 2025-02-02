/**
 * @file    engine_controller.h
 * @brief   Controllers package entry point header
 *
 * @date Feb 7, 2013
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#pragma once

// todo: huh we also have validateConfiguration()?!
bool validateConfig();
char * getPinNameByAdcChannel(const char *msg, adc_channel_e hwChannel, char *buffer);
void initEngineController();
void commonInitEngineController();
void initStartStopButton();

void initDataStructures();

void slowStartStopButtonCallback();

void doPeriodicSlowCallback();
