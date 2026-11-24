#include "pch.h"

#include "init.h"
#include "frequency_sensor.h"
#include "vehicle_speed_converter.h"

FrequencySensor vehicleSpeedSensor(SensorType::VehicleSpeed, MS2NT(500));
static VehicleSpeedConverter vehicleSpeedConverter;

void initVehicleSpeedSensor() {
	vehicleSpeedSensor.initIfValid(
			engineConfiguration->vehicleSpeedSensorInputPin, vehicleSpeedConverter, engineConfiguration->vssWindowMs);
}

void deInitVehicleSpeedSensor() {
	vehicleSpeedSensor.deInit();
}
