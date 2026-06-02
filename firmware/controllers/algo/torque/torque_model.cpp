#include "pch.h"

#include "throttle_model.h"
#include "gppwm_channel.h"

#include <algorithm>

void TorqueModelBase::onFastCallback() {
	if (!engineConfiguration->enableTorqueModel) {
		return;
	}

	// Collect demands
	float driverTorqueDemand = driverDemand();
	m_driverTorqueDemand = driverTorqueDemand;

	// TODO: take the max of other demand sources, revmatch, idle, etc
	float torqueRequested = driverTorqueDemand;
	m_torqueRequested = torqueRequested;

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
	m_airmassTarget = totalAirmassTarget;

	// Hand the target off to the airmass path (real impl drives the ETB)
	commandAirmass(totalAirmassTarget);
}

void TorqueModel::commandAirmass(float totalAirmassTarget) {
	airmassDispatcher.update(totalAirmassTarget);

	// Logging
	m_airmassActual = airmassDispatcher.getActualAirmass();
	m_airmassTrim = airmassDispatcher.getAirmassTrim();
	m_throttleRequest = airmassDispatcher.getThrottleRequest();
}

float TorqueModel::driverDemand() const {
	float rpm = Sensor::getOrZero(SensorType::Rpm);

	// Same pedal source and sanitization as the ETB setpoint path.
	float pedal = clampF(0, Sensor::get(SensorType::AcceleratorPedal).value_or(0), 100);

	return interpolate3d(
			config->driverTorqueTable, config->driverTorquePedalBins, pedal, config->driverTorqueRpmBins, rpm);
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
	auto gearRatio = engine->module<GearDetector>()->getTotalRatioInCurrentGear();
	if (gearRatio) {
		// For example, 1000Nm axle limit and 8:1 total gear ratio becomes 125Nm engine limit
		auto axleLimitAtEngine = tm.axleMaximum / gearRatio.Value;
		LIMITER(axleLimitAtEngine, limitedByAxleMax);
	} else {
		limitedByAxleMax = false;
	}

	auto evalGenericLimit = [&](size_t i) -> float {
		auto& lim = engineConfiguration->torqueLimiters[i];
		if (!lim.enable) {
			m_limiterXAxisValue[i] = 0;
			m_limiterYAxisValue[i] = 0;
			m_limiterTorque[i] = 0;
			return 0;
		}

		float x = readGppwmChannel(lim.xAxis).value_or(0);
		float y = readGppwmChannel(lim.yAxis).value_or(0);
		m_limiterXAxisValue[i] = x;
		m_limiterYAxisValue[i] = y;

		// Table is indexed [y][x]: y is the row axis, x is the column axis.
		float limit = interpolate3d(lim.table, lim.yBins, y, lim.xBins, x);
		m_limiterTorque[i] = limit;

		if (lim.applyAtAxle) {
			// Axle-domain: scale to the engine using the current gear ratio. If the ratio is
			// unknown (neutral/stopped/no gear detection), suppress the limit rather than guess.
			if (!gearRatio) {
				return 0;
			}
			limit /= gearRatio.Value;
		}

		return limit;
	};

	// LIMITER evaluates its first arg twice, so cache the per-table lookup first.
	float limit1 = evalGenericLimit(0);
	float limit2 = evalGenericLimit(1);
	float limit3 = evalGenericLimit(2);
	float limit4 = evalGenericLimit(3);
	LIMITER(limit1, limitedByGenericLimiter1);
	LIMITER(limit2, limitedByGenericLimiter2);
	LIMITER(limit3, limitedByGenericLimiter3);
	LIMITER(limit4, limitedByGenericLimiter4);

	return result;
}

float TorqueModel::getTorqueLoss() {
	float rpm = Sensor::getOrZero(SensorType::Rpm);

	// The Y axis is user-selectable (default coolant temperature); fall back to 0 if its
	// source sensor is unavailable so the lookup still uses the first load column.
	float loadAxis = readGppwmChannel(engineConfiguration->torqueModel.torqueLossLoadAxis).value_or(0);
	m_torqueLossLoadAxisValue = loadAxis;

	// torqueLossTable is indexed [load][rpm]: load is the row (Y) axis, rpm the column (X).
	return interpolate3d(config->torqueLossTable, config->torqueLossLoadBins, loadAxis, config->torqueLossRpmBins, rpm);
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
