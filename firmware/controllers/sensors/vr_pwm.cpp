#include "pch.h"

#include "vr_pwm.h"

static OutputPin pins[VR_THRESHOLD_COUNT];
static SimplePwm pwms[VR_THRESHOLD_COUNT];

// Default to 3.3v if not defined, most boards wire the VR threshold input directly to an MCU pin.
#ifndef VR_SUPPLY_VOLTAGE
#define VR_SUPPLY_VOLTAGE 3.3f
#endif

static void updateVrPwm(float rpm, size_t index) {
	auto& cfg = config->vrThreshold[index];

	if (!isBrainPinValid(cfg.pin)) {
		return;
	}

	float thresholdVoltage = interpolate2d(rpm, cfg.rpmBins, cfg.values);

	// 0v   threshold voltage = 3.3v output from mcu = 100% duty
	// 2.5v threshold voltage = 0v   output from mcu = 0% duty
	float thresholdInputVoltage = interpolateClamped(0, 3.3f, 2.5f, 0, thresholdVoltage);

	float duty = thresholdInputVoltage / VR_SUPPLY_VOLTAGE;

	pwms[index].setSimplePwmDutyCycle(duty);
}

void updateVrPwm() {
	auto rpm = Sensor::getOrZero(SensorType::Rpm);

	for (size_t i = 0; i < efi::size(config->vrThreshold); i++) {
		updateVrPwm(rpm, i);
	}
}

void initVrPwm() {
	for (size_t i = 0; i < efi::size(config->vrThreshold); i++) {
		auto& cfg = config->vrThreshold[i];

		if (!isBrainPinValid(cfg.pin)) {
			continue;
		}

		startSimplePwmHard(&pwms[i], "VR PWM",
			cfg.pin,
			&pins[i],
			10000,	// it's guaranteed to be hardware PWM, the faster the PWM, the less noise makes it through
			0
		);
	}
}

void setDefaultVrThresholds() {
	for (int i = 0; i < VR_THRESHOLD_COUNT; i++) {
		setLinearCurve(config->vrThreshold[i].rpmBins, 600, 7000, 100);
		setLinearCurve(config->vrThreshold[i].values, 0.6, 1.2, 0.1);
	}
}
