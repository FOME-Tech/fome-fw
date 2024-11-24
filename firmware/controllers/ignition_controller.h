#pragma once

#include "engine_module.h"

#include "timer.h"

class IgnitionController : public EngineModule {
public:
	using interface_t = IgnitionController;

	void onSlowCallback() override;

	virtual bool getIgnState() const {
		return m_lastState;
	}

private:
	Timer m_timeSinceIgnVoltage;
	bool m_lastState = false;
};
