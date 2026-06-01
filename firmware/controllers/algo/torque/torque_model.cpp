#include "pch.h"

float TorqueModel::driverDemand() {
	float rpm = Sensor::getOrZero(SensorType::Rpm);

	// Same pedal source and sanitization as the ETB setpoint path.
	float pedal = clampF(0, Sensor::get(SensorType::AcceleratorPedal).value_or(0), 100);

	// driverTorqueTable is indexed [pedal][rpm], reusing the ETB pedal-table axes.
	float value = interpolate3d(
			config->driverTorqueTable, config->pedalToTpsPedalBins, pedal, config->pedalToTpsRpmBins, rpm);

	driverTorqueDemand = value;

	return value;
}
