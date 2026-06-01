#include "pch.h"

#include "throttle_model.h"

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
	airmassDispatcher.update(totalAirmassTarget);
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

void AirmassDispatcher::update(float targetAirmassPerCycle) {
	float rpm = Sensor::getOrZero(SensorType::Rpm);

	// No airflow if the engine isn't turning or nothing is being requested.
	if (rpm <= 0 || targetAirmassPerCycle <= 0) {
		m_throttleRequest = 0;
		return;
	}

	if (!engineConfiguration->twoStroke) {
		// 4-stroke engines only induct on half of crank revolutions
		targetAirmassPerCycle /= 2;
	}
	float targetFlow = targetAirmassPerCycle * rpm / 60;

	// Throttle inlet pressure: real TIP sensor, else baro, else standard atmosphere.
	float tip = Sensor::hasSensor(SensorType::ThrottleInletPressure)
					  ? Sensor::getOrZero(SensorType::ThrottleInletPressure)
					  : Sensor::get(SensorType::BarometricPressure).value_or(101.325f);

	float iat = Sensor::get(SensorType::Iat).value_or(20);

	// The throttle's flow for a given angle depends on the pressure ratio across it.
	// Use the measured MAP rather than inverting VE for a target: it needs no VE table
	// (not every airmass mode has a credible one), it's exact in steady state, and the
	// loop converges on its own as the throttle opens and the manifold fills.
	float map = Sensor::getOrZero(SensorType::Map);
	float pressureRatio = map / tip;

	// Let the throttle model saturate on its own: it returns 100% when the requested flow
	// exceeds wide-open capacity, and clamps the pressure-ratio correction internally (so
	// map >= tip is safe). We deliberately do NOT short-circuit to wide-open when map nears
	// tip - the manifold also sits near atmospheric at key-on / cranking / closed-throttle
	// decel, and commanding wide-open throttle in those conditions would be dangerous.
	m_throttleRequest = engine->module<ThrottleModel>()->throttlePositionForFlow(targetFlow, pressureRatio, tip, iat);
}

percent_t AirmassDispatcher::getThrottleRequest() {
	return m_throttleRequest;
}
