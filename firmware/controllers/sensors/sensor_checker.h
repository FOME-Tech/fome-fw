#pragma once

// TODO: this name is now probably wrong, since it checks injectors/ignition too
struct SensorChecker : public EngineModule {
public:
	void onSlowCallback() override;
	void onIgnitionStateChanged(bool ignitionOn) override;

	bool analogSensorsShouldWork() const {
		return m_analogSensorsShouldWork;
	}

	void onGoodKnockSensorSignal(uint8_t channelIdx, efitick_t knockSenseTime);

private:
	bool m_ignitionIsOn = false;
	Timer m_timeSinceIgnOff;
	Timer m_timeSinceVbattLow;

	Timer m_lastGoodKnockSampleTimer[2];

	bool m_analogSensorsShouldWork = false;
};
