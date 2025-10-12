
#include "pch.h"
#include "instant_rpm_calculator.h"

#if EFI_SHAFT_POSITION_INPUT

InstantRpmCalculator::InstantRpmCalculator() :
	//https://en.cppreference.com/w/cpp/language/zero_initialization
	timeOfLastEvent()
{
}

void InstantRpmCalculator::movePreSynchTimestamps() {
	// here we take timestamps of events which happened prior to synchronization and place them
	// at appropriate locations
	auto triggerSize = getTriggerCentral()->triggerShape.getLength();

	size_t eventsToCopy = minI(spinningEventIndex, triggerSize);

	size_t firstSrc;
	size_t firstDst;

	if (eventsToCopy >= triggerSize) {
		// Only copy one trigger length worth of events, filling the whole buffer
		firstSrc = spinningEventIndex - triggerSize;
		firstDst = 0;
	} else {
		// There is less than one full cycle, copy to the end of the buffer
		firstSrc = 0;
		firstDst = triggerSize - spinningEventIndex;
	}

	memmove(timeOfLastEvent + firstDst, timeOfLastEvent + firstSrc, eventsToCopy * sizeof(timeOfLastEvent[0]));
}

expected<float> InstantRpmCalculator::calculateInstantRpm(
	TriggerWaveform const & triggerShape, TriggerFormDetails *triggerFormDetails,
	uint32_t current_index, uint32_t nowNt32, angle_t window) const {

	// Determine where we currently are in the revolution
	angle_t currentAngle = triggerFormDetails->eventAngles[current_index];
	efiAssert(ObdCode::OBD_PCM_Processor_Fault, !std::isnan(currentAngle), "eventAngles", 0);

	// Hunt for a tooth ~90 degrees ago to compare to the current time
	angle_t previousAngle = currentAngle - window;
	wrapAngle(previousAngle, "prevAngle", ObdCode::CUSTOM_ERR_TRIGGER_ANGLE_RANGE);
	int prevIndex = triggerShape.findAngleIndex(triggerFormDetails, previousAngle);

	// now let's get precise angle for that event
	angle_t prevIndexAngle = triggerFormDetails->eventAngles[prevIndex];
	auto time90ago = timeOfLastEvent[prevIndex];

	// No previous timestamp, instant RPM isn't ready yet
	if (time90ago == 0) {
		return unexpected;
	}

	uint32_t time = nowNt32 - time90ago;
	angle_t angleDiff = currentAngle - prevIndexAngle;

	// Wrap the angle in to the correct range (ie, could be -630 when we want +90)
	wrapAngle(angleDiff, "angleDiff", ObdCode::CUSTOM_ERR_6561);

	// just for safety, avoid divide-by-0
	if (time == 0) {
		return unexpected;
	}

	float instantRpm = (60000000.0 / 360 * US_TO_NT_MULTIPLIER) * angleDiff / time;

	// This fixes early RPM instability based on incomplete data
	if (instantRpm < RPM_LOW_THRESHOLD) {
		return unexpected;
	}

	return instantRpm;
}

void InstantRpmCalculator::setLastEventTimeForInstantRpm(efitick_t nowNt) {
	// here we remember tooth timestamps which happen prior to synchronization
	if (spinningEventIndex >= efi::size(timeOfLastEvent)) {
		// too many events while trying to find synchronization point
		// todo: better implementation would be to shift here or use cyclic buffer so that we keep last
		// 'PRE_SYNC_EVENTS' events
		return;
	}

	uint32_t nowNt32 = nowNt;
	timeOfLastEvent[spinningEventIndex] = nowNt32;

	// If we are using only rising edges, we never write in to the odd-index slots that
	// would be used by falling edges
	// TODO: don't reach across to trigger central to get this info
	spinningEventIndex += getTriggerCentral()->triggerShape.useOnlyRisingEdges ? 2 : 1;
}

void InstantRpmCalculator::updateInstantRpm(
	TriggerWaveform const & triggerShape, TriggerFormDetails *triggerFormDetails,
	uint32_t index, const EnginePhaseInfo& phaseInfo) {

	// It's OK to truncate from 64b to 32b, ARM with single precision FPU uses an expensive
	// software function to convert 64b int -> float, while 32b int -> float is very cheap hardware conversion
	// The difference is guaranteed to be short (it's 90 degrees of engine rotation!), so it won't overflow.
	uint32_t nowNt32 = phaseInfo.timestamp;

	assertIsInBounds(index, timeOfLastEvent, "calc timeOfLastEvent");

	// Record the time of this event so we can calculate RPM from it later
	timeOfLastEvent[index] = nowNt32;

	auto instantRpm = calculateInstantRpm(triggerShape, triggerFormDetails, index, nowNt32, engineConfiguration->instantRpmRange);
	if (instantRpm) {
		m_instantRpm = instantRpm.Value;
	}
}

void InstantRpmCalculator::updateCylinderContribution(TriggerWaveform const & triggerShape, TriggerFormDetails *triggerFormDetails,
	uint32_t current_index, uint32_t nowNt32, angle_t window, const EnginePhaseInfo& phase) {
	int minTriggerLength = engineConfiguration->cylindersCount * 2;
	if (triggerShape.getLength() < minTriggerLength) {
		// Not enough teeth to reasonably determine cylinder contribution
		return;
	}

	float measurementWindow = engineConfiguration->cylContributionWindow;
	float measurementOffset = engineConfiguration->cylContributionPhase;

	if (measurementWindow <= 0) {
		return;
	}

	for (size_t i = 0; i < engineConfiguration->cylindersCount; i++) {
		auto& cyl = engine->cylinders[i];

		auto measurementAngle = cyl.getAngleOffset() + measurementOffset;
		wrapAngle(measurementAngle, "misfire angle", ObdCode::OBD_PCM_Processor_Fault);

		if (isPhaseInRange(EngPhase{measurementAngle}, phase)) {
			if (auto rpm = calculateInstantRpm(triggerShape, triggerFormDetails, current_index, nowNt32, measurementWindow)) {
				efiPrintf("******************** Cyl %d RPM %.1f (delta)", i + 1, rpm.Value, (rpm.Value - m_lastCylRpm));

				engine->outputChannels.cylinderRpm[i] = rpm.Value;
				engine->outputChannels.cylinderRpmDelta[i] = rpm.Value - m_lastCylRpm;
				m_lastCylRpm = rpm.Value;
			}
		}
	}
}

#endif // EFI_SHAFT_POSITION_INPUT