#pragma once

#include <cstdint>
#include <rusefi/expected.h>

// What the clutch is doing, decided from whichever clutch inputs are configured. A single switch
// can't tell all three positions apart, so the "Or" states carry what it can tell.
enum class ClutchState : uint8_t {
	// No usable clutch information: no input, sensor fault, or both switches on
	Indeterminate = 0,
	// Clutch fully applied, engine is driving the wheels
	Engaged = 1,
	// Somewhere between applied and released, may be slipping
	Middle = 2,
	// Clutch fully released, engine is decoupled from the wheels
	Disengaged = 3,
	// Only a down switch, and it isn't pressed
	EngagedOrMiddle = 4,
	// Only an up switch, and the pedal isn't up
	MiddleOrDisengaged = 5,
};

// Decide the clutch state from the analog position sensor and the up/down switches. A valid analog
// position wins, the switches are the fallback. An invalid up/down means that switch isn't
// configured.
ClutchState decideClutchState(expected<float> analog, expected<bool> up, expected<bool> down);

// The state decided on the last slow callback
ClutchState getClutchState();

// True if the clutch is known to not be fully applied (it may be slipping or released)
bool isClutchKnownNotEngaged(ClutchState state);

// True if the clutch is known to be fully released
bool isClutchKnownDisengaged(ClutchState state);

// Claim the configured clutch switch pins as inputs
void startClutchPins();

// Release the clutch switch pins from the previous configuration
void stopClutchPins();
