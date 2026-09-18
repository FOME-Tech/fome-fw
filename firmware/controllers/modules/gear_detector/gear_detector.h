#pragma once

class GearDetector : public EngineModule, public Sensor {
public:
	GearDetector();
	~GearDetector();

	void onSlowCallback() override;
	void onConfigurationChange(engine_configuration_s const* /*previousConfig*/) override;

	float getGearboxRatio() const;

	// Returns 0 for neutral, 1 for 1st, 5 for 5th, etc.
	size_t determineGearFromRatio(float ratio) const;

	// Same, but biased in favor of stickyGear: the measured ratio has to move past the boundary
	// between two gears by a margin before we'll consider leaving the gear we're holding.
	// Pass 0 for stickyGear to hold no gear, ie plain nearest-gear classification.
	size_t determineGearFromRatio(float ratio, size_t stickyGear) const;

	// How far off the measured ratio is from where the given gear should be, as a fraction:
	// 0.05 means the measurement is 5% away from that gear's configured ratio. Returns a large
	// number for a gear that can't be measured against (neutral, or out of range).
	float gearRatioDeviation(float ratio, size_t gear) const;

	float getRpmInGear(size_t gear) const;

	expected<float> getTotalRatioInCurrentGear() const;

	// Sensor implementation
	SensorResult get() const override;
	void showInfo(const char* sensorName) const override;

private:
	float computeGearboxRatio() const;
	float getDriveshaftRpm() const;

	float m_gearboxRatio = 0;
	size_t m_currentGear = 0;

	// How well the ratio we're measuring right now matches the gear we think we're in
	float m_gearDeviation = 0;

	// Gear we're considering switching to, and how long it's been the only thing we've seen
	size_t m_candidateGear = 0;
	Timer m_candidateTimer;

	float m_gearThresholds[GEARS_COUNT - 1];
};
