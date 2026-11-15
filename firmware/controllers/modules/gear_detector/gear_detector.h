#pragma once

#include "gear_detector_state_generated.h"

enum class ClutchState : uint8_t {
	// No usable clutch information: no sensor, sensor fault, or misconfigured thresholds
	Indeterminate = 0,
	// Clutch fully applied, engine is driving the wheels
	Engaged = 1,
	// Somewhere between applied and released, may be slipping
	Middle = 2,
	// Clutch fully released, engine is decoupled from the wheels
	Disengaged = 3,
};

// Decide what the clutch is doing from whatever clutch inputs are configured. Currently only the
// analog position sensor is used. Future sources (clutch up and/or down switches) also map onto
// this enum: for example an up switch alone can only distinguish Engaged from Middle.
ClutchState decideClutchPosition();

class GearDetector : public EngineModule, public Sensor, public gear_detector_state_s {
public:
	GearDetector();
	~GearDetector();

	void onSlowCallback() override;
	void onConfigurationChange(engine_configuration_s const* /*previousConfig*/) override;

	float getGearboxRatio() const;

	// Returns 0 for neutral, 1 for 1st, 5 for 5th, etc.
	size_t determineGearFromRatio(float ratio) const;

	// Returns the gear whose ratio is within a tight tolerance of the measured ratio, or 0 if
	// the ratio isn't close to any gear (clutch slip, neutral, mid-shift).
	size_t determineLockedGear(float ratio) const;

	float getRpmInGear(size_t gear) const;

	expected<float> getTotalRatioInCurrentGear() const;

	// Current clutch state, read live (not at the gear detector's update rate) for fast consumers.
	// Indeterminate unless the clutch sensor is currently trusted.
	ClutchState getTrustedClutchState() const;

	// Sensor implementation
	SensorResult get() const override;
	void showInfo(const char* sensorName) const override;

private:
	float computeGearboxRatio() const;
	float getDriveshaftRpm() const;

	void updateClutchTrust(ClutchState clutch, size_t locked);
	size_t determineGearWithClutch(ClutchState clutch, size_t locked) const;
	bool isLockCoupled() const;

	float m_gearboxRatio = 0;
	size_t m_currentGear = 0;

	float m_gearThresholds[GEARS_COUNT - 1];
	float m_lockTolerance = 0.04f;

	// An "episode" is a span where both the locked gear and the clutch state are unchanged.
	Timer m_episodeTimer;
	float m_episodeRpmMin = 0;
	float m_episodeRpmMax = 0;

	// Set when the clutch was seen disengaged with the engine decoupled from the wheels, cleared
	// once the clutch is engaged in a gear again. That full cycle is what earns clutch trust.
	bool m_sawDecoupled = false;
};
