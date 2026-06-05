#include "pch.h"

#include "throttle_model.h"
#include "gppwm_channel.h"

#include <algorithm>

void TorqueModelBase::onFastCallback() {
	if (!engineConfiguration->enableTorqueModel) {
		return;
	}

	// Forward calculation of current torque
	float airmassActual = engine->fuelComputer.sdAirMassInOneCylinder * engineConfiguration->cylindersCount;
	m_airmassActual = airmassActual;

	float torqueLoss = getTorqueLoss();
	m_torqueLoss = torqueLoss;

	// Torque management
	float grossTorqueRequest = calculateGrossTorqueRequest(torqueLoss);

	// Convert to airmass and drive throttle
	{
		// for now, 90Nm/gram is an OK hack
		float totalAirmassTarget = grossTorqueRequest / 90;
		m_airmassTarget = totalAirmassTarget;

		// Hand the target off to the airmass path (real impl drives the ETB)
		commandAirmass(totalAirmassTarget, airmassActual);
	}
}

float TorqueModelBase::calculateGrossTorqueRequest(float torqueLoss) {
	// Collect demands
	float driverTorqueDemand = driverDemand();
	m_driverTorqueDemand = driverTorqueDemand;

	auto idleTorqueDemand = idleDemand(driverTorqueDemand);
	m_idleTorqueDemand = idleTorqueDemand.value_or(0);

	// Arbitrate demands: deliver whatever the highest demand asks for.
	// TODO: take the max of other demand sources too, revmatch, cruise, etc
	float torqueRequested = std::max(driverTorqueDemand, idleTorqueDemand.value_or(-10000));
	m_torqueRequested = torqueRequested;

	// Apply any limiting to the requested torque
	float torqueRequestedLimited = applyTorqueLimits(torqueRequested);
	m_torqueRequestedLimited = torqueRequestedLimited;

	// add any torque loss
	float grossTorque = torqueRequestedLimited + torqueLoss;
	m_grossTorque = grossTorque;

	return grossTorque;
}

void TorqueModel::commandAirmass(float totalAirmassTarget, float actualAirmassPerCycle) {
	airmassDispatcher.update(totalAirmassTarget, actualAirmassPerCycle);

	// Logging
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

expected<float> TorqueModel::idleDemand(float driverDemand) {
#if EFI_IDLE_CONTROL
	// Bind the PID to its config on first use / after a config change (mirrors the IAC idle reset).
	if (!m_idleTorquePid.isSame(&engineConfiguration->torqueModel.idlePid)) {
		m_idleTorquePid.initPidClass(&engineConfiguration->torqueModel.idlePid);
	}

	// The torque-land analog of "pedal above the idle deactivation threshold": the driver has lifted
	// off the idle governor once asking for more torque than idle was providing. Feeding that to the
	// phase machine makes the idle->driver handoff happen exactly where the two demands are equal, so
	// it's bumpless - there's no sliver of pedal that requests less torque than idle and stalls.
	bool driverAboveIdle = driverDemand > m_idleGovernorTorque;

	// Idle target RPM and phase come from the shared IdleTargetController (runs earlier this tick).
	auto idleState = engine->module<IdleTargetController>()->getOutput(driverAboveIdle);

	// Only close the loop while actually idling - same gate as the IAC closed loop. Off-idle we emit
	// unexpected so the idle control output is ignored. The feed-forward to hold idle is the loss table, which is added
	// to the arbitrated demand downstream, so the target brake torque here is just 0 Nm.
	if (idleState.phase != IIdleController::Phase::Idling) {
		// Don't carry stale correction into the next idle entry. Keep a positive I-term (it props RPM
		// up on return to idle); drop a negative one (it would fight the return).
		if (m_idleTorquePid.getIntegration() <= 0 || engineConfiguration->alwaysResetPidLeavingIdle) {
			m_idleTorquePid.reset();
		}
		m_idleGovernorTorque = 0;
		return unexpected;
	}

	float rpm = engine->triggerCentral.instantRpm.getInstantRpm();
	float rpmRate = engine->rpmCalculator.getRpmAcceleration();

	// Clamp the integrator to the configured output authority so it can't wind up past it.
	m_idleTorquePid.iTermMin = engineConfiguration->torqueModel.idlePid.minValue;
	m_idleTorquePid.iTermMax = engineConfiguration->torqueModel.idlePid.maxValue;

	// Override the D term with the measured RPM rate, avoiding derivative kick from quantized RPM.
	m_idleTorquePid.setDTermOverride(-rpmRate);

	// Remember what idle is providing: this is the threshold the driver has to exceed to lift off, so
	// it must reflect the governor value even across ticks where idle isn't winning the arbitration.
	m_idleGovernorTorque =
			m_idleTorquePid.getOutput(idleState.target.ClosedLoopTarget, rpm, FAST_CALLBACK_PERIOD_MS / 1000.0f);
	return m_idleGovernorTorque;
#else
	return 0;
#endif // EFI_IDLE_CONTROL
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

void AirmassDispatcher::update(float targetAirmassPerCycle, float actualAirmassPerCycle) {
	float rpm = Sensor::getOrZero(SensorType::Rpm);

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
	float error = targetAirmassPerCycle - actualAirmassPerCycle;
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
