/**
 * @file    allsensors.cpp
 * @brief
 *
 *
 * @date Nov 15, 2013
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"

ButtonDebounce acDebounce("ac_switch");
ButtonDebounce acPressureDebounce("ac_pressure_switch");

void initSensors() {
	initMapDecoder();
	acDebounce.init(MS2NT(15), engineConfiguration->acSwitch, engineConfiguration->acSwitchMode);
	acPressureDebounce.init(MS2NT(15), engineConfiguration->acPressureSwitch, engineConfiguration->acPressureSwitchMode);
}

bool getAcToggle() {
	return acDebounce.readPinState();
}

bool hasAcToggle() {
	return isBrainPinValid(engineConfiguration->acSwitch);
}

bool hasAcPressure() {
	return isBrainPinValid(engineConfiguration->acPressureSwitch);
}

bool getAcPressure() {
	return acPressureDebounce.readPinState();
}