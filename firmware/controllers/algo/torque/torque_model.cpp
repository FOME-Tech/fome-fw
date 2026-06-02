#include "pch.h"

#include "throttle_model.h"

#include <algorithm>

void TorqueModel::onFastCallback() {
	if (!engineConfiguration->enableTorqueModel) {
		return;
	}

	// Collect demands
	float driverTorqueDemand = driverDemand();
	m_driverTorqueDemand = driverTorqueDemand;

	// TODO: take the max of other demand sources, revmatch, idle, etc
	float torqueRequested = driverTorqueDemand;

	// Apply any limiting to the requested torque
	float torqueRequestedLimited = applyTorqueLimits(torqueRequested);
	m_torqueRequestedLimited = torqueRequestedLimited;

	// add any torque loss
	float torqueLoss = getTorqueLoss();
	m_torqueLoss = torqueLoss;
	float grossTorque = torqueRequestedLimited + torqueLoss;
	m_grossTorque = grossTorque;

	// for now, 90Nm/gram is an OK hack
	float totalAirmassTarget = grossTorque / 90;

	// Update outputs
	airmassDispatcher.update(totalAirmassTarget);

	// Logging
	m_airmassTarget = totalAirmassTarget;
	m_airmassActual = airmassDispatcher.getActualAirmass();
	m_airmassTrim = airmassDispatcher.getAirmassTrim();
	m_throttleRequest = airmassDispatcher.getThrottleRequest();
}

float TorqueModel::driverDemand() const {
	float rpm = Sensor::getOrZero(SensorType::Rpm);

	// Same pedal source and sanitization as the ETB setpoint path.
	float pedal = clampF(0, Sensor::get(SensorType::AcceleratorPedal).value_or(0), 100);

	// driverTorqueTable is indexed [pedal][rpm], reusing the ETB pedal-table axes.
	return interpolate3d(config->driverTorqueTable, config->pedalToTpsPedalBins, pedal, config->pedalToTpsRpmBins, rpm);
}

float TorqueModel::applyTorqueLimits(const float torqueRequested) {
	float result = torqueRequested;

	const auto& tm = engineConfiguration->torqueModel;

#define LIMITER(limitVal, resultBit)                                                                                   \
	do {                                                                                                               \
		if (limitVal != 0 && torqueRequested > limitVal) {                                                             \
			result = std::min(result, (float)limitVal);                                                                \
			resultBit = true;                                                                                          \
		} else {                                                                                                       \
			resultBit = false;                                                                                         \
		}                                                                                                              \
	} while (false);

	LIMITER(tm.engineMaximum, limitedByEngineMax);

	// getTotalRatioInCurrentGear returns unexpected if not configured, in neutral, or stopped
	if (auto ratio = engine->module<GearDetector>()->getTotalRatioInCurrentGear()) {
		// For example, 1000Nm axle limit and 8:1 total gear ratio becomes 125Nm engine limit
		auto axleLimitAtEngine = tm.axleMaximum / ratio.Value;
		LIMITER(axleLimitAtEngine, limitedByAxleMax);
	} else {
		limitedByAxleMax = false;
	}

	return result;
}

float TorqueModel::getTorqueLoss() const {
	// TODO!
	return 0;
}

percent_t TorqueModel::getThrottleRequest() {
	return airmassDispatcher.getThrottleRequest();
}

void AirmassDispatcher::update(float targetAirmassPerCycle) {
	float rpm = Sensor::getOrZero(SensorType::Rpm);

	// Measured airmass for the closed-loop trim - the same speed-density quantity fueling
	// uses, scaled from per-cylinder up to whole-engine per cycle.
	m_actualAirmass = engine->fuelComputer.sdAirMassInOneCylinder * engineConfiguration->cylindersCount;

	// No airflow if the engine isn't turning or nothing is being requested. Park the trim
	// integrator at zero so it can't wind up while the throttle is held closed.
	if (rpm <= 0 || targetAirmassPerCycle <= 0) {
		m_throttleRequest = 0;
		m_airmassTrim = 0;
		m_trimITerm = 0;
		return;
	}

	// Closed-loop trim: a small PI that drives measured airmass to target. The output is a
	// percent correction on the commanded flow, so the throttle model's inverse provides the
	// gain scheduling and the PI sees a near-linear plant.
	auto& trimCfg = engineConfiguration->torqueModel;
	float error = targetAirmassPerCycle - m_actualAirmass;
	float dt = FAST_CALLBACK_PERIOD_MS / 1000.0f;
	float authority = trimCfg.airmassTrimAuthority;

	// Integrate, clamping the accumulator to the trim authority so it can't wind past achievable trim.
	m_trimITerm = clampF(-authority, m_trimITerm + trimCfg.airmassTrimKi * error * dt, authority);
	m_airmassTrim = clampF(-authority, trimCfg.airmassTrimKp * error + m_trimITerm, authority);

	float airmassPerCycle = targetAirmassPerCycle * (1 + m_airmassTrim * PERCENT_DIV);

	if (!engineConfiguration->twoStroke) {
		// 4-stroke engines only induct on half of crank revolutions
		airmassPerCycle /= 2;
	}
	float targetFlow = airmassPerCycle * rpm / 60;

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

percent_t AirmassDispatcher::getThrottleRequest() const {
	return m_throttleRequest;
}

float AirmassDispatcher::getAirmassTrim() const {
	return m_airmassTrim;
}

float AirmassDispatcher::getActualAirmass() const {
	return m_actualAirmass;
}
