#include "pch.h"

void TorqueModel::onFastCallback() {
	if (!engineConfiguration->enableTorqueModel) {
		return;
	}

	// Collect demands
	float driverTorque = driverDemand();

	// TODO the interesting middle bit
	// for now, 90Nm/gram is an OK hack
	float totalAirmassTarget = driverTorque / 90;

	// Update outputs
	airmassDispatcher.update(airmassTarget);
}

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

percent_t TorqueModel::getThrottleRequest() {
	return airmassDispatcher.getThrottleRequest();
}

void AirmassDispatcher::update(float targetAirmass) {
	// TODO!
}

percent_t AirmassDispatcher::getThrottleRequest() {
	return 0;
}
