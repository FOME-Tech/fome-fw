#pragma once

#include "main_loop.h"
#include "obd_error_codes.h"

/**
 * Requires a fault to persist for a while before it's allowed to set a DTC.
 *
 * A single bad reading is far more likely to be noise than a real failure, so we
 * only latch a code once the same fault has been reported on every check for
 * roughly a second.  Codes that stop being reported leak back down again.
 */
class DtcDebouncer {
public:
	// Report that this code is faulted right now.
	// Returns true once the fault has persisted long enough to be considered real.
	bool report(ObdCode code);

	// Call once per check cycle, after all report() calls, to age out codes
	// that weren't reported this cycle.
	void endCycle();

	// Forget everything - used when checking is inhibited entirely
	void reset();

private:
	// MAX_ERROR_CODES_COUNT is 10, leave a little headroom
	static constexpr size_t maxCodes = 12;
	// One second's worth of checks
	static constexpr uint8_t threshold = static_cast<uint8_t>(1000 / SLOW_CALLBACK_PERIOD_MS);

	struct Entry {
		ObdCode code = ObdCode::None;
		uint8_t count = 0;
		bool seenThisCycle = false;
	};

	Entry m_entries[maxCodes];
};

// TODO: this name is now probably wrong, since it checks injectors/ignition too
struct SensorChecker : public EngineModule {
public:
	void onSlowCallback() override;
	void onIgnitionStateChanged(bool ignitionOn) override;

	bool analogSensorsShouldWork() const {
		return m_analogSensorsShouldWork;
	}

	void onKnockSensorSignal(float dbv, uint8_t channelIdx, efitick_t knockSenseTime);

private:
	// Should we be checking sensors at all right now? False means the sensors we'd be
	// checking may legitimately have no power, so any fault we see means nothing.
	bool shouldCheckSensors();

	bool m_ignitionIsOn = false;
	Timer m_timeSinceIgnOff;
	Timer m_timeSinceVbattLow;

	Timer m_lastGoodKnockSampleTimer[2];
	bool m_hasSeenKnockSensor[2];

	bool m_analogSensorsShouldWork = false;

	DtcDebouncer m_dtcDebounce;
};
