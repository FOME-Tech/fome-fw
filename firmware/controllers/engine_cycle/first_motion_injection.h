/**
 * @file first_motion_injection.h
 *
 * Starts cranking fuel as soon as the engine starts to turn, rather than waiting for trigger sync.
 */

#pragma once

#include "engine_module.h"
#include "rusefi_types.h"

class FirstMotionInjection : public EngineModule {
public:
	// Called for every decoded trigger tooth, synchronized or not
	void onTriggerTooth();

	void onFastCallback() override;

	// Added to the cranking cycle counter when looking up the cranking cycle multiplier: 1 if this
	// start's first cranking cycle was already injected on first motion, otherwise 0
	uint32_t getCrankingCycleOffset() const {
		return m_fired ? 1 : 0;
	}

	// Fuel mass per cylinder the first motion pulse would inject right now
	mass_t getFuelMass() const {
		return m_fuelMass;
	}

	// Teeth the engine has to turn from a standstill before the pulse, so that a single noise pulse or
	// a nudge doesn't inject
	static constexpr uint8_t teethBeforePulse = 3;

private:
	mass_t computeFuelMass() const;
	void fire();

	// Teeth seen since the engine last stood still
	uint8_t m_teethSinceStandstill = 0;
	// The pulse has been tried since the engine last stood still (only once per start)
	bool m_tried = false;
	// The pulse was injected since the engine last stood still
	bool m_fired = false;

	mass_t m_fuelMass = 0;
};
