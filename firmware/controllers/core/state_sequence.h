/**
 * @file	state_sequence.h
 *
 * @date May 18, 2014
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#pragma once

#include <stdint.h>
#include "rusefi_enums.h"
#include <rusefi/expected.h>

// see also 'HW_EVENT_TYPES'
enum class TriggerEvent {
	PrimaryFalling = 0,
	PrimaryRising = 1,
	SecondaryFalling = 2,
	SecondaryRising = 3,
};

/**
 * This layer has two primary usages:
 * 1) 'simple' PWM generation is used to produce actuator square control wave
 * 2) 'complex' PWM generation is used for trigger simulator.
 * Some triggers like Nissan 360 slot optical wheel need a lot of points to describe the shape of the wave.
 * Looks like 252 is explained by 60 tooth * 2 (number of fronts) * 2 (number of crank rotations within engine cycle)
 */
#ifndef PWM_PHASE_MAX_COUNT
// Maximum events in one revolution of a trigger wheel. The largest built-in pattern is 60-2 at 116
// events, and a custom toothed wheel tops out at 60 teeth (120 events).
#define PWM_PHASE_MAX_COUNT 128
#endif /* PWM_PHASE_MAX_COUNT */
#define PWM_PHASE_MAX_WAVE_PER_PWM 2

/**
 * This class represents multi-channel logical signals with shared time axis
 *
 * This is a semi-abstract interface so that implementations can exist for either regularized
 * patterns (60-2, etc) or completely arbitrary patterns stored in arrays.
 */
class MultiChannelStateSequence {
public:
	/**
	 * values in the (0..1] range which refer to points within the period at at which pin state
	 * should be changed So, in the simplest case we turn pin off at 0.3 and turn it on at 1 -
	 * that would give us a 70% duty cycle PWM
	 */
	virtual float getSwitchTime(int phaseIndex) const = 0;
	virtual bool getChannelState(int channelIndex, int phaseIndex) const = 0;

	// Make sure the switch times are in order and end at the very end.
	void checkSwitchTimes(float scale) const;

	// Find the exact angle, or unexpected if it doesn't exist
	expected<int> findAngleMatch(float angle) const;

	// returns the index at which given value would need to be inserted into sorted array
	int findInsertionAngle(float angle) const;

	uint16_t phaseCount = 0; // Number of timestamps
	uint16_t waveCount = 0;	 // Number of waveforms
};

/**
 * A sequence whose arrays belong to someone else, so that sequences of different capacities can share
 * one type. The owner provides the arrays and is responsible for the bounds.
 */
class ExternalMultiChannelStateSequence : public MultiChannelStateSequence {
public:
	ExternalMultiChannelStateSequence(float* switchTimes, uint8_t* waveForm)
		: m_switchTimes(switchTimes)
		, m_waveForm(waveForm) {}

	float getSwitchTime(int phaseIndex) const override {
		return m_switchTimes[phaseIndex];
	}

	bool getChannelState(int channelIndex, int phaseIndex) const override {
		return (m_waveForm[phaseIndex] >> channelIndex) & 1;
	}

	void reset() {
		waveCount = 0;
	}

	void setSwitchTime(const int phaseIndex, const float value) {
		m_switchTimes[phaseIndex] = value;
	}

	void setChannelState(const int channelIndex, const int phaseIndex, bool state) {
		uint8_t& ref = m_waveForm[phaseIndex];
		ref = (ref & ~(1U << channelIndex)) | ((state ? 1 : 0) << channelIndex);
	}

private:
	float* const m_switchTimes;
	uint8_t* const m_waveForm;
};

template <unsigned max_phase>
class MultiChannelStateSequenceWithData : public MultiChannelStateSequence {
public:
	float getSwitchTime(int phaseIndex) const override {
		return switchTimes[phaseIndex];
	}

	bool getChannelState(int channelIndex, int phaseIndex) const override {
		if (channelIndex >= waveCount) {
			// todo: would be nice to get this asserting working
			// firmwareError("channel index %d/%d", channelIndex, waveCount);
		}
		return (waveForm[phaseIndex] >> channelIndex) & 1;
	}

	void reset() {
		waveCount = 0;
	}

	void setSwitchTime(const int phaseIndex, const float value) {
		switchTimes[phaseIndex] = value;
	}

	void setChannelState(const int channelIndex, const int phaseIndex, bool state) {
		if (channelIndex >= waveCount) {
			// todo: would be nice to get this asserting working
			// firmwareError("channel index %d/%d", channelIndex, waveCount);
		}
		uint8_t& ref = waveForm[phaseIndex];
		ref = (ref & ~(1U << channelIndex)) | ((state ? 1 : 0) << channelIndex);
	}

private:
	float switchTimes[max_phase];
	uint8_t waveForm[max_phase];
};
