#include "pch.h"

#include "speedometer.h"

static SimplePwm speedoPwm("speedo");

static ConfiguredPwm speedoOutput(speedoPwm);

void speedoUpdate() {
	if (!speedoOutput.isRunning()) {
		return;
	}

	float kph = Sensor::getOrZero(SensorType::VehicleSpeed);
	float kps = kph * (1. / 3600);
	float freq = kps * engineConfiguration->speedometerPulsePerKm;

	if (freq < 1) {
		freq = NAN;
	}

	speedoPwm.setFrequency(freq);
}

void stopSpeedometer(const engine_configuration_s* /*prev*/) {
	speedoOutput.stopIfUnwanted(isBrainPinValid(engineConfiguration->speedometerOutputPin));
}

void startSpeedometer(const engine_configuration_s* prev) {
	// Frequency is set by speedoUpdate
	speedoOutput.startIfWanted(
			prev,
			isBrainPinValid(engineConfiguration->speedometerOutputPin),
			"Speedometer",
			&enginePins.speedoOut,
			NAN,
			0.5f);
}
