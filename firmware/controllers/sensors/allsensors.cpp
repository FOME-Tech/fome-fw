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
static ButtonDebounce pgInput1("PG_input_1");
static ButtonDebounce pgInput2("PG_input_2");

void initSensors() {
	initMapDecoder();
	acDebounce.init(MS2NT(15), engineConfiguration->acSwitch, engineConfiguration->acSwitchMode);
	acPressureDebounce.init(MS2NT(15), engineConfiguration->acPressureSwitch, engineConfiguration->acPressureSwitchMode);
	oilPressureSwitchDebounce.init(MS2NT(15), engineConfiguration->oilPressureSwitch, engineConfiguration->oilPressureSwitchMode);

	if(isBrainPinValid(engineConfiguration->pgPins[0]) && isBrainPinValid(engineConfiguration->pgPins[1]))
	{
		auto type = PI_PULLUP;
		pgInput1.init(MS2NT(15), engineConfiguration->pgPins[0], type);
		pgInput2.init(MS2NT(15), engineConfiguration->pgPins[1], type);
	}
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

bool getPgstate(uint8_t index) {
	switch (index) {
		case 0:
			return pgInput1.readPinState();
		case 1:
			return pgInput2.readPinState();
		default:
			return false;
	}
}