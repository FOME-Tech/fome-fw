/**
 * @file    allsensors.cpp
 * @brief
 *
 *
 * @date Nov 15, 2013
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"

static ButtonDebounce acDebounce("ac_switch");
static ButtonDebounce acPressureDebounce("ac_pressure_switch");
static ButtonDebounce oilPressureSwitchDebounce("oil_pressure_switch");

void initSensors() {
	initMapDecoder();
	acDebounce.init(MS2NT(15), engineConfiguration->acSwitch, engineConfiguration->acSwitchMode);
	acPressureDebounce.init(
			MS2NT(15), engineConfiguration->acPressureSwitch, engineConfiguration->acPressureSwitchMode);
	oilPressureSwitchDebounce.init(
			MS2NT(15), engineConfiguration->oilPressureSwitch, engineConfiguration->oilPressureSwitchMode);
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

bool hasOilPressureSwitch() {
	return isBrainPinValid(engineConfiguration->oilPressureSwitch);
}

bool getOilSwitchState() {
	return oilPressureSwitchDebounce.readPinState();
}
