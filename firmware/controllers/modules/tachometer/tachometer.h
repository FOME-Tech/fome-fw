/*
 * @file tachometer.h
 *
 * @date Aug 18, 2015
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#pragma once

// Configuration sequence step, see config_steps.cpp
void stopTachometer(const engine_configuration_s* prev);
void startTachometer(const engine_configuration_s* prev);

class TachometerModule : public EngineModule {
public:
	// TODO: can/should this be slow callback instead?
	void onFastCallback() override;
	void onIgnitionStateChanged(bool ignitionOn) override;

private:
	float getRpm();

	bool m_doTachSweep = false;

	enum class TachState {
		Normal,
		RampUp,
		RampDown,
	};

	TachState m_state = TachState::Normal;
	Timer m_stateChangeTimer;
};
