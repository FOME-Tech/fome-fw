/**
 * @file engine_module.h
 */

#pragma once

#include "engine_configuration.h"
#include "engine_phase_angle.h"

class EngineModule {
public:
	// Called exactly once during boot, before configuration is loaded
	virtual void initNoConfiguration() {}

	// Called when 'Burn' is invoked
	virtual void onConfigurationChange(engine_configuration_s const* /*previousConfig*/) {}

	// Called approx 20Hz
	virtual void onSlowCallback() {}

	// Called approx 250Hz
	virtual void onFastCallback() {}

	// First RPM / changed trigger configuration: prepare synchronous outputs before
	// the next engine phase. New modules conservatively retain their fast work.
	// Only modules with no first-cycle scheduling/protection dependency may opt out.
	virtual void onSynchronousFastCallback() {
		onFastCallback();
	}

	// Called when the engine stops. Reset your state, etc to prepare for the next start.
	virtual void onEngineStop() {}

	// Called whenever the ignition switch state changes
	virtual void onIgnitionStateChanged(bool /*ignitionOn*/) {}

	// Queried to determine whether this module needs a delayed shutoff, defaults to false
	virtual bool needsDelayedShutoff() {
		return false;
	}

	// Called on every successfully decoded tooth of the primary trigger
	virtual void onEnginePhase(float /*rpm*/, const EnginePhaseInfo& /*phase*/) {}
};
