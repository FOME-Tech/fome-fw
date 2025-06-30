#pragma once

#include "engine_module.h"
#include "main_relay_generated.h"

class MainRelayController : public EngineModule, public main_relay_s {
public:
	void onSlowCallback() override;
	bool needsDelayedShutoff() override;
	void benchTest();

private:
	Timer m_benchTestTimer;

	Timer m_lastIgnitionTime;
	Timer m_relayOnTimer;
};
