#include "pch.h"
#include "init.h"
#include "functional_sensor.h"

static FunctionalSensor fuelFlowSensor(SensorType::FuelFlow, /* timeout = */ MS2NT(500));

void initFuelFlowSensor() {
	fuelFlowSensor.Register();
}