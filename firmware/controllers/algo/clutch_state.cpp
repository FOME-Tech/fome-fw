#include "pch.h"

#include "clutch_state.h"

static expected<ClutchState> decideFromAnalog(expected<float> analog) {
	if (!analog) {
		return unexpected;
	}

	float appliedBelow = engineConfiguration->clutchAppliedBelow;
	float releasedAbove = engineConfiguration->clutchReleasedAbove;

	if (appliedBelow >= releasedAbove) {
		// Misconfigured (or not configured at all)
		return unexpected;
	}

	if (analog.Value < appliedBelow) {
		return ClutchState::Engaged;
	}

	if (analog.Value > releasedAbove) {
		return ClutchState::Disengaged;
	}

	return ClutchState::Middle;
}

static ClutchState decideFromSwitches(expected<bool> up, expected<bool> down) {
	if (up.Valid && down.Valid) {
		if (up.Value && down.Value) {
			// The pedal can't be both up and down
			warning(ObdCode::OBD_Clutch_Switch_Input, "Clutch up and down switches are both on");
			return ClutchState::Indeterminate;
		}

		if (up.Value) {
			return ClutchState::Engaged;
		}

		return down.Value ? ClutchState::Disengaged : ClutchState::Middle;
	}

	if (up.Valid) {
		return up.Value ? ClutchState::Engaged : ClutchState::MiddleOrDisengaged;
	}

	if (down.Valid) {
		return down.Value ? ClutchState::Disengaged : ClutchState::EngagedOrMiddle;
	}

	return ClutchState::Indeterminate;
}

ClutchState decideClutchState(expected<float> analog, expected<bool> up, expected<bool> down) {
	if (auto fromAnalog = decideFromAnalog(analog)) {
		return fromAnalog.Value;
	}

	return decideFromSwitches(up, down);
}

ClutchState getClutchState() {
	return engine->engineState.clutchState;
}

bool isClutchKnownNotEngaged(ClutchState state) {
	switch (state) {
		case ClutchState::Middle:
		case ClutchState::Disengaged:
		case ClutchState::MiddleOrDisengaged:
			return true;
		default:
			return false;
	}
}

bool isClutchKnownDisengaged(ClutchState state) {
	return state == ClutchState::Disengaged;
}

void startClutchPins() {
#if EFI_PROD_CODE
	// On Miata the clutch down input is wired both to the clutch pedal and the gearbox neutral switch
	efiSetPadMode(
			"clutch down switch",
			engineConfiguration->clutchDownPin,
			getInputMode(engineConfiguration->clutchDownPinMode));
	efiSetPadMode(
			"clutch up switch", engineConfiguration->clutchUpPin, getInputMode(engineConfiguration->clutchUpPinMode));
#endif // EFI_PROD_CODE
}

void stopClutchPins() {
	// Return a pin that's no longer used as before to its idle state
	if (isPinOrModeChanged(clutchUpPin, clutchUpPinMode)) {
		efiSetPadUnused(activeConfiguration.clutchUpPin);
	}

	if (isPinOrModeChanged(clutchDownPin, clutchDownPinMode)) {
		efiSetPadUnused(activeConfiguration.clutchDownPin);
	}

	brain_pin_markUnused(activeConfiguration.clutchUpPin);
	brain_pin_markUnused(activeConfiguration.clutchDownPin);
}
