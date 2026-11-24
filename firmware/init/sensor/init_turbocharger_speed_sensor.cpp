#include "pch.h"

/**
 * See init_input_shaft_speed_sensor.cpp concerns - those might apply here as well!
 */

#include "init.h"
#include "frequency_sensor.h"
#include "turbocharger_speed_converter.h"

static FrequencySensor turbochargerSpeedSensor(SensorType::TurbochargerSpeed, MS2NT(500));
static TurbochargerSpeedConverter turbochargerSpeedConverter;

void initTurbochargerSpeedSensor() {
	turbochargerSpeedSensor.initIfValid(
			engineConfiguration->turboSpeedSensorInputPin,
			turbochargerSpeedConverter,
			engineConfiguration->turboSpeedWindowMs);
}

void deinitTurbochargerSpeedSensor() {
	turbochargerSpeedSensor.deInit();
}
