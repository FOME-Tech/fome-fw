#include "pch.h"

#include "main_relay.h"

void MainRelayController::onSlowCallback() {
	isBenchTest = engine->isInMainRelayBench();

#if EFI_MAIN_RELAY_CONTROL
	hasIgnitionVoltage = Sensor::getOrZero(SensorType::BatteryVoltage) > 5;

	if (hasIgnitionVoltage) {
		m_lastIgnitionTime.reset();
	}

	// Query whether any engine modules want to keep the lights on
	delayedShutoffRequested = engine->engineModules.aggregate([](auto& m, bool prev) { return m.needsDelayedShutoff() | prev; }, false);

	// TODO: delayed shutoff timeout?

	mainRelayState = isBenchTest | hasIgnitionVoltage | delayedShutoffRequested;
#else // not EFI_MAIN_RELAY_CONTROL
	mainRelayState = !isBenchTest;
#endif

	enginePins.mainRelay.setValue(mainRelayState);

	if (!mainRelayState) {
		// Reset the on timer while off
		m_relayOnTimer.reset();
	}

	// If we have a main relay (input) voltage sensor, check that the main relay works
	if (Sensor::hasSensor(SensorType::MainRelayVoltage)) {
		if (m_relayOnTimer.hasElapsedSec(0.5f)) {
			float mainRelayVolts = Sensor::getOrZero(SensorType::MainRelayVoltage);
			float batteryVolts = Sensor::getOrZero(SensorType::BatteryVoltage);

			if (batteryVolts - mainRelayVolts > 3) {
				warning(ObdCode::OBD_PCM_MainRelayFault, "Main relay fault! VBatt: %.1fv, MR: %.1fv", batteryVolts, mainRelayVolts);
			}
		}
	}
}

bool MainRelayController::needsDelayedShutoff() {
	// Prevent main relay from turning off if we had igniton voltage in the past 1 second
	// This avoids accidentally killing the car during a transient, for example
	// right when the starter is engaged.
	return !m_lastIgnitionTime.hasElapsedSec(1);
}
