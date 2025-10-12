/**
 * instant_rpm_calculator.h
 */

#pragma once
#include "trigger_structure.h"

class InstantRpmCalculator {
public:
	InstantRpmCalculator();
	float getInstantRpm() const {
		return m_instantRpm;
	}

#if EFI_ENGINE_CONTROL && EFI_SHAFT_POSITION_INPUT
	void updateInstantRpm(
		TriggerWaveform const & triggerShape, TriggerFormDetails *triggerFormDetails,
		uint32_t index, const EnginePhaseInfo& phaseInfo);
#endif
	/**
	 * Update timeOfLastEvent[] on every trigger event - even without synchronization
	 * Needed for early spin-up RPM detection.
	 */
	void setLastEventTimeForInstantRpm(efitick_t nowNt);

	void movePreSynchTimestamps();

	void resetInstantRpm() {
		setArrayValues(timeOfLastEvent, 0);
		spinningEventIndex = 0;
		m_instantRpm = 0;
	}

	/**
	 * timestamp of each trigger wheel tooth
	 */
	uint32_t timeOfLastEvent[PWM_PHASE_MAX_COUNT];

	size_t spinningEventIndex = 0;

	float m_instantRpm = 0;
private:
	expected<float> calculateInstantRpm(
		TriggerWaveform const & triggerShape, TriggerFormDetails *triggerFormDetails,
		uint32_t index, uint32_t nowNt32, angle_t window) const;

	void updateCylinderContribution(TriggerWaveform const & triggerShape, TriggerFormDetails *triggerFormDetails,
		uint32_t current_index, uint32_t nowNt32, const EnginePhaseInfo& phase);

	float m_lastCylRpm = 0;
};
