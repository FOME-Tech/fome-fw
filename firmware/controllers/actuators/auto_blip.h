#pragma once

#include "auto_blip_generated.h"

class AutoBlip : public EngineModule, public auto_blip_s {
public:
	void onFastCallback() override;

	float getEtbAdjustment() const;

	enum class State {
		// No blip is happening
		Idle,

		// The driver is braking, but hasn't pushed the clutch yet
		Armed,

		// We are currently applying a blip
		Blip,

		// The blip ran for the allowed time without the driver releasing the clutch
		BlipTimeout,

		// The driver held the brake without pressing the clutch for too long - wait for the brake to be
		// released before arming again, so a stale clutch press later doesn't trigger a surprise blip
		ArmTimedOut
	};

	State
	nextState(State currentState, bool brakeDown, bool clutchDown, bool blipAllowed, const auto_blip_cfg_s& cfg) const;

	// Returns true if a blip is allowed for the given conditions
	bool blipAllowed(size_t targetGear, float currentRpm, float targetRpm, float vehicleSpeed);

	bool requestCut(float rpm);

private:
	void onStateChange(State newState);

	State m_state = State::Idle;
	Timer m_timeInState;

	size_t m_targetGear = 0;

	class {
	public:
		void set(float val) {
			m_value = val;
			m_timer.reset();
		}

		float get() const {
			if (!m_timer.hasElapsedSec(0.1f)) {
				return m_value;
			}

			return 0;
		}

	private:
		float m_value;
		Timer m_timer;
	} m_etbAdjust;
};
