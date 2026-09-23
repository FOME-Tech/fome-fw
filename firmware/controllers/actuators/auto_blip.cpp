#include "pch.h"

AutoBlip::State AutoBlip::nextState(
		State currentState, bool brakeDown, bool clutchDown, bool blipAllowed, const auto_blip_cfg_s& cfg) const {
	// In any state, releasing the brake (or disabling the feature) returns to the idle state
	if (!cfg.enabled || !brakeDown) {
		return State::Idle;
	}

	switch (currentState) {
		case State::Idle:
			// Only arm if the clutch isn't already down - a blip should be triggered by the driver pushing the
			// clutch after braking, not by braking while already clutched in.
			if (!clutchDown) {
				return State::Armed;
			}
			break;
		case State::Armed:
			if (blipAllowed && clutchDown) {
				return State::Blip;
			}

			// The driver took too long to push the clutch after braking - don't fire a stale blip later.
			// Stay here (rather than Idle) until the brake is released, so we don't just re-arm next cycle.
			if (m_timeInState.hasElapsedSec(cfg.armTimeout)) {
				return State::ArmTimedOut;
			}

			break;
		case State::Blip:
			// If still braking but the clutch is released, return to armed (the driver might make another downshift
			// during this braking event)
			if (!clutchDown) {
				return State::Armed;
			}

			if (m_timeInState.hasElapsedSec(cfg.blipTime)) {
				return State::BlipTimeout;
			}

			break;
		case State::BlipTimeout:
			if (!clutchDown) {
				return State::Armed;
			}
			break;
		case State::ArmTimedOut:
			// Nothing to do here - only a brake release (handled above) gets us out of this state.
			break;
	}

	return currentState;
}

void AutoBlip::onStateChange(State /*newState*/) {
	m_timeInState.reset();
}

void AutoBlip::onFastCallback() {
	const auto& cfg = engineConfiguration->autoBlip;

	// collect inputs
	float rpm = Sensor::getOrZero(SensorType::Rpm);
	bool brakeDown = engine->engineState.brakePedalState;
	bool clutchDown = engine->engineState.clutchDownState;
	float vss = Sensor::getOrZero(SensorType::VehicleSpeed);

	// Compute target RPM
	m_targetRpm = engine->module<GearDetector>()->getRpmInGear(m_targetGear);

	// Determine if blip is currently allowed
	m_allowBlip = blipAllowed(m_targetGear, rpm, m_targetRpm, vss);

	// decide next state
	State next = nextState(m_state, brakeDown, clutchDown, m_allowBlip, cfg);
	if (m_state != next) {
		onStateChange(next);
		m_state = next;
	}

	// Drive outputs
	if (m_state == State::Blip) {
		m_etbAdjust.set(cfg.blipThrottleAdd);
	} else {
		m_etbAdjust.set(0);
	}

	// Latch target gear

	// TODO: smarter latching logic. Currently, this just latches the target gear whenever the clutch down switch isn't
	// pressed. However, this is dodgy, as the clutch will slip before the clutch down switch is pressed (the clutch is
	// already fully released by the time the switch is pressed).
	if (!clutchDown) {
		size_t currentGear = engine->module<GearDetector>()->get().value_or(0);
		// Applying a blip in 1st is meaningless, only blip when downshifting from 2nd or higher
		if (currentGear >= 2) {
			m_targetGear = currentGear - 1;
		} else {
			m_targetGear = 0;
		}
	}
}

bool AutoBlip::requestCut(float rpm) {
	float cutThreshold = m_targetRpm + engineConfiguration->autoBlip.cutThresholdRpm;

	bool cut = m_state == State::Blip && rpm > cutThreshold;
	m_requestCut = cut;
	return cut;
}

float AutoBlip::getEtbAdjustment() const {
	return m_etbAdjust.get();
}

bool AutoBlip::blipAllowed(size_t targetGear, float currentRpm, float targetRpm, float vehicleSpeed) {
	const auto& cfg = engineConfiguration->autoBlip;

	m_targetGearTooLow = targetGear < cfg.minTargetGear;
	m_currentRpmTooLow = currentRpm < cfg.minCurrentRpm;
	m_targetRpmTooLow = targetRpm < cfg.minTargetRpm;
	m_vehicleSpeedTooLow = vehicleSpeed < cfg.minVehicleSpeed;
	auto clt = Sensor::get(SensorType::Clt);
	m_cltTooLow = !clt || clt.Value < cfg.minClt;

	if (m_targetGearTooLow) {
		return false;
	}

	if (m_currentRpmTooLow) {
		return false;
	}

	if (m_targetRpmTooLow) {
		return false;
	}

	if (m_vehicleSpeedTooLow) {
		return false;
	}

	if (m_cltTooLow) {
		return false;
	}

	return true;
}
