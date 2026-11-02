#include "pch.h"

AutoBlip::State AutoBlip::nextState(
		State currentState, bool brakeDown, ClutchState clutch, bool blipAllowed, const auto_blip_cfg_s& cfg) const {
	// In any state, releasing the brake, disabling the feature, or losing trust in the clutch sensor returns to
	// the idle state
	if (!cfg.enabled || !brakeDown || clutch == ClutchState::Indeterminate) {
		return State::Idle;
	}

	bool clutchDisengaged = clutch == ClutchState::Disengaged;

	switch (currentState) {
		case State::Idle:
			// Only arm with the clutch fully engaged - a blip should be triggered by the driver pushing the
			// clutch after braking, not by braking while already clutched in. This also means that after a
			// blip, the clutch has to come all the way back up before another can fire.
			if (clutch == ClutchState::Engaged) {
				return State::Armed;
			}
			break;
		case State::Armed:
			if (blipAllowed && clutchDisengaged) {
				return State::Blip;
			}

			// The driver took too long to push the clutch after braking - don't fire a stale blip later.
			// Stay here (rather than Idle) until the brake is released, so we don't just re-arm next cycle.
			if (m_timeInState.hasElapsedSec(cfg.armTimeout)) {
				return State::ArmTimedOut;
			}

			break;
		case State::Blip:
			// The clutch is starting to bite, the blip is over. Return to idle, which re-arms once the clutch is
			// fully engaged (the driver might make another downshift during this braking event).
			if (!clutchDisengaged) {
				return State::Idle;
			}

			if (m_timeInState.hasElapsedSec(cfg.blipTime)) {
				return State::BlipTimeout;
			}

			break;
		case State::BlipTimeout:
			if (!clutchDisengaged) {
				return State::Idle;
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
	ClutchState clutch = engine->module<GearDetector>()->getTrustedClutchState();
	float vss = Sensor::getOrZero(SensorType::VehicleSpeed);

	// Gear detection holds the gear we're shifting out of from the moment the clutch leaves engaged until it
	// re-engages in a new gear, so the target is simply one below the detected gear.
	auto currentGear = static_cast<size_t>(Sensor::getOrZero(SensorType::DetectedGear));
	// Applying a blip in 1st is meaningless, only blip when downshifting from 2nd or higher
	m_targetGear = currentGear >= 2 ? currentGear - 1 : 0;

	// Compute target RPM
	m_targetRpm = engine->module<GearDetector>()->getRpmInGear(m_targetGear);

	// Determine if blip is currently allowed
	m_allowBlip = blipAllowed(m_targetGear, rpm, m_targetRpm, vss);

	// decide next state
	State next = nextState(m_state, brakeDown, clutch, m_allowBlip, cfg);
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
