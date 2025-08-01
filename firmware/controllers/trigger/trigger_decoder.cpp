/**
 * @file	trigger_decoder.cpp
 *
 * @date Dec 24, 2013
 * @author Andrey Belomutskiy, (c) 2012-2020
 *
 * This file is part of rusEfi - see http://rusefi.com
 *
 * rusEfi is free software; you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * rusEfi is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 * even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include "pch.h"

#include "global_shared.h"
#include "engine_configuration.h"

/**
 * decoder uses TriggerStimulatorHelper in findTriggerZeroEventIndex
 */
#include "trigger_simulator.h"

TriggerDecoderBase::TriggerDecoderBase(const char* name)
	: m_name(name)
{
	resetState();
}

bool TriggerDecoderBase::getShaftSynchronized() const {
	return shaft_is_synchronized;
}

void TriggerDecoderBase::setShaftSynchronized(bool value) {
	shaft_is_synchronized = value;
}

void TriggerDecoderBase::resetState() {
	setShaftSynchronized(false);
	toothed_previous_time = {};

	setArrayValues(toothDurations, 0);

	crankSynchronizationCounter = 0;
	triggerErrorCounter = 0;
	orderingErrorCounter = 0;
	m_timeSinceDecodeError.init();

	prevSignal = TriggerEvent::PrimaryFalling;
	startOfCycleNt = {};

	resetCurrentCycleState();

	totalEventCountBase = 0;
	isFirstEvent = true;
}

void TriggerDecoderBase::setTriggerErrorState() {
	m_timeSinceDecodeError.reset();
	triggerErrorCounter++;
}

void TriggerDecoderBase::resetCurrentCycleState() {
	setArrayValues(currentCycle.eventCount, 0);
	currentCycle.current_index = 0;
}

#if EFI_SHAFT_POSITION_INPUT

PrimaryTriggerDecoder::PrimaryTriggerDecoder(const char* name)
	: TriggerDecoderBase(name)
{
}

#if ! EFI_PROD_CODE
bool printTriggerDebug = false;
bool printTriggerTrace = false;
#endif /* ! EFI_PROD_CODE */

void TriggerWaveform::initializeSyncPoint(TriggerDecoderBase& state,
			const TriggerConfiguration& triggerConfiguration) {
	triggerShapeSynchPointIndex = state.findTriggerZeroEventIndex(*this, triggerConfiguration);
}

void TriggerFormDetails::prepareEventAngles(TriggerWaveform *shape) {
	if (!shape->triggerShapeSynchPointIndex) {
		return;
	}

	auto triggerShapeSynchPointIndex = shape->triggerShapeSynchPointIndex.Value;

	angle_t firstAngle = shape->getAngle(triggerShapeSynchPointIndex);
	assertAngleRange(firstAngle, "firstAngle", ObdCode::CUSTOM_TRIGGER_SYNC_ANGLE);

	int riseOnlyIndex = 0;

	size_t length = shape->getLength();

	setArrayValues(eventAngles, 0);

	// this may be <length for some triggers like symmetrical crank Miata NB
	size_t triggerShapeLength = shape->getSize();

	efiAssertVoid(ObdCode::CUSTOM_TRIGGER_CYCLE, getTriggerCentral()->engineCycleEventCount != 0, "zero engineCycleEventCount");

	for (size_t eventIndex = 0; eventIndex < length; eventIndex++) {
		if (eventIndex == 0) {
			// explicit check for zero to avoid issues where logical zero is not exactly zero due to float nature
			eventAngles[0] = 0;
			// this value would be used in case of front-only
			eventAngles[1] = 0;
		} else {
			// Rotate the trigger around so that the sync point is at position 0
			auto wrappedIndex = (triggerShapeSynchPointIndex + eventIndex) % length;

			// Compute this tooth's position within the trigger definition
			// (wrap, as the trigger def may be smaller than total trigger length)
			auto triggerDefinitionIndex = wrappedIndex % triggerShapeLength;

			// Compute the relative angle of this tooth to the sync point's tooth
			float angle = shape->getAngle(wrappedIndex) - firstAngle;

			efiAssertVoid(ObdCode::CUSTOM_TRIGGER_CYCLE, !std::isnan(angle), "trgSyncNaN");
			// Wrap the angle back in to [0, 720)
			wrapAngle(angle, "trgSync", ObdCode::CUSTOM_TRIGGER_SYNC_ANGLE_RANGE);

			if (shape->useOnlyRisingEdges) {
				efiAssertVoid(ObdCode::OBD_PCM_Processor_Fault, triggerDefinitionIndex < triggerShapeLength, "trigger shape fail");
				assertIsInBounds(triggerDefinitionIndex, shape->isRiseEvent, "isRise");

				// In case this is a rising event, replace the following fall event with the rising as well
				if (shape->isRiseEvent[triggerDefinitionIndex]) {
					riseOnlyIndex += 2;
					eventAngles[riseOnlyIndex] = angle;
					eventAngles[riseOnlyIndex + 1] = angle;
				}
			} else {
				eventAngles[eventIndex] = angle;
			}
		}
	}
}

int64_t TriggerDecoderBase::getTotalEventCounter() const {
	return totalEventCountBase + currentCycle.current_index;
}

int TriggerDecoderBase::getCrankSynchronizationCounter() const {
	return crankSynchronizationCounter;
}

void PrimaryTriggerDecoder::resetState() {
	TriggerDecoderBase::resetState();

	resetHasFullSync();
}

bool TriggerDecoderBase::isValidIndex(const TriggerWaveform& triggerShape) const {
	return currentCycle.current_index < triggerShape.getSize();
}

static const TriggerWheel eventIndex[4] = { TriggerWheel::T_PRIMARY, TriggerWheel::T_PRIMARY, TriggerWheel::T_SECONDARY, TriggerWheel:: T_SECONDARY };
static const bool eventType[4] = { false, true, false, true };

#if EFI_UNIT_TEST
#define PRINT_INC_INDEX 		if (printTriggerTrace) {\
		printf("nextTriggerEvent index=%d\r\n", currentCycle.current_index); \
		}
#else
#define PRINT_INC_INDEX {}
#endif /* EFI_UNIT_TEST */

#define nextTriggerEvent() \
 { \
	if (useOnlyRisingEdgeForTrigger) {currentCycle.current_index++;} \
	currentCycle.current_index++; \
	PRINT_INC_INDEX; \
}

int TriggerDecoderBase::getCurrentIndex() const {
	return currentCycle.current_index;
}

angle_t PrimaryTriggerDecoder::syncEnginePhase(int divider, int remainder, angle_t engineCycle) {
	efiAssert(ObdCode::OBD_PCM_Processor_Fault, divider > 1, "syncEnginePhase divider", false);
	efiAssert(ObdCode::OBD_PCM_Processor_Fault, remainder < divider, "syncEnginePhase remainder", false);

	auto currentRemainder = getCrankSynchronizationCounter() % divider;
	auto totalShift = (remainder - currentRemainder) * engineCycle / divider;

	if (totalShift < 0) {
		totalShift += engineCycle;
	}

	{
		chibios_rt::CriticalSectionLocker csl;

		// Allow injection/ignition to happen, we've now fully sync'd the crank based on new cam information
		m_hasSynchronizedPhase = true;

		if (m_phaseAdjustment != totalShift) {
			// Resync angle changed - count how many times this happens
			m_phaseAdjustment = totalShift;
			m_camResyncCounter++;
		}
	}

	return totalShift;
}

// Returns true if syncEnginePhase has been called,
// i.e. if we have enough VVT information to have full sync on
// an indeterminite crank pattern
// If we're self stimulating, assume we have full sync so that outputs work during self stim
bool PrimaryTriggerDecoder::hasSynchronizedPhase() const {
#if EFI_PROD_CODE
	if (getTriggerCentral()->directSelfStimulation && engineConfiguration->fakeFullSyncForStimulation) {
		return true;
	}
#endif

	return m_hasSynchronizedPhase;
}

void PrimaryTriggerDecoder::onTriggerError() {
	// On trigger error, we've lost full sync
	resetHasFullSync();

	// Ignore the warning that engine is never null - it might be in unit tests
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Waddress"
		if (engine) {
			// Instant RPM data is now also probably trash, discard it
			engine->triggerCentral.instantRpm.resetInstantRpm();
			engine->rpmCalculator.lastTdcTimer.init();
		}
	#pragma GCC diagnostic pop
}

void PrimaryTriggerDecoder::onNotEnoughTeeth(int /*actual*/, int /*expected*/) {
	warning(ObdCode::CUSTOM_PRIMARY_NOT_ENOUGH_TEETH, "primary trigger error: not enough teeth between sync points: expected %d/%d got %d/%d",
			getTriggerCentral()->triggerShape.getExpectedEventCount(TriggerWheel::T_PRIMARY),
			getTriggerCentral()->triggerShape.getExpectedEventCount(TriggerWheel::T_SECONDARY),
		currentCycle.eventCount[0],
		currentCycle.eventCount[1]);
}

void PrimaryTriggerDecoder::onTooManyTeeth(int /*actual*/, int /*expected*/) {
	warning(ObdCode::CUSTOM_PRIMARY_TOO_MANY_TEETH, "primary trigger error: too many teeth between sync points: expected %d/%d got %d/%d",
			getTriggerCentral()->triggerShape.getExpectedEventCount(TriggerWheel::T_PRIMARY),
			getTriggerCentral()->triggerShape.getExpectedEventCount(TriggerWheel::T_SECONDARY),
		currentCycle.eventCount[0],
		currentCycle.eventCount[1]);
}

const char *getTriggerEvent(TriggerEvent value){
switch(value) {
case TriggerEvent::PrimaryFalling:
  return "SHAFT_PRIMARY_FALLING";
case TriggerEvent::PrimaryRising:
  return "SHAFT_PRIMARY_RISING";
case TriggerEvent::SecondaryFalling:
  return "SHAFT_SECONDARY_FALLING";
case TriggerEvent::SecondaryRising:
  return "SHAFT_SECONDARY_RISING";
  }
 return NULL;
}

void VvtTriggerDecoder::onNotEnoughTeeth(int actual, int expected) {
	warning(ObdCode::CUSTOM_CAM_NOT_ENOUGH_TEETH, "cam %s trigger error: not enough teeth between sync points: actual %d expected %d", m_name, actual, expected);
}

void VvtTriggerDecoder::onTooManyTeeth(int actual, int expected) {
	warning(ObdCode::CUSTOM_CAM_TOO_MANY_TEETH, "cam %s trigger error: too many teeth between sync points: %d > %d", m_name, actual, expected);
}

bool TriggerDecoderBase::validateEventCounters(const TriggerWaveform& triggerShape) const {
	// We can check if things are fine by comparing the number of events in a cycle with the expected number of event.
	bool isDecodingError = false;
	for (int i = 0; i < PWM_PHASE_MAX_WAVE_PER_PWM; i++) {
		isDecodingError |= (currentCycle.eventCount[i] != triggerShape.getExpectedEventCount((TriggerWheel)i));
	}

#if EFI_UNIT_TEST
	printf("validateEventCounters: isDecodingError=%d\n", isDecodingError);
	if (isDecodingError) {
		for (int i = 0; i < PWM_PHASE_MAX_WAVE_PER_PWM; i++) {
			printf("count: cur=%d exp=%d\n", currentCycle.eventCount[i],  triggerShape.getExpectedEventCount((TriggerWheel)i));
		}
	}
#endif /* EFI_UNIT_TEST */

	return isDecodingError;
}

void TriggerDecoderBase::onShaftSynchronization(
		bool wasSynchronized,
		const efitick_t nowNt,
		const TriggerWaveform& triggerShape) {
	startOfCycleNt = nowNt;
	resetCurrentCycleState();

	if (wasSynchronized) {
		crankSynchronizationCounter++;
	} else {
		// We have just synchronized, this is the zeroth revolution
		crankSynchronizationCounter = 0;
	}

	totalEventCountBase += triggerShape.getSize();

#if EFI_UNIT_TEST
	if (printTriggerDebug) {
		printf("onShaftSynchronization index=%d %d\r\n",
				currentCycle.current_index,
				crankSynchronizationCounter);
	}
#endif /* EFI_UNIT_TEST */
}

static bool shouldConsiderEdge(const TriggerWaveform& triggerShape, TriggerWheel triggerWheel, bool isRising) {
	if (triggerWheel != TriggerWheel::T_PRIMARY && triggerShape.useOnlyPrimaryForSync) {
		// Non-primary events ignored 
		return false;
	}

	switch (triggerShape.m_syncEdge) {
		case SyncEdge::Both: return true;
		case SyncEdge::RiseOnly:
		case SyncEdge::Rise: return isRising;
		case SyncEdge::Fall: return !isRising;
	}

	// how did we get here?
	// assert(false)?

	return false;
}

void TriggerDecoderBase::logEdgeCounters(bool isRising) {
	if (isRising) {
		edgeCountRise++;
	} else {
		edgeCountFall++;
	}
}

/**
 * @brief Trigger decoding happens here
 * VR falls are filtered out and some VR noise detection happens prior to invoking this method, for
 * Hall this method is invoked every time we have a fall or rise on one of the trigger sensors.
 * This method changes the state of trigger_state_s data structure according to the trigger event
 * @param signal type of event which just happened
 * @param nowNt current time
 */
expected<TriggerDecodeResult> TriggerDecoderBase::decodeTriggerEvent(
		const char *msg,
		const TriggerWaveform& triggerShape,
		TriggerStateListener* triggerStateListener,
		const TriggerConfiguration& triggerConfiguration,
		const TriggerEvent signal,
		const efitick_t nowNt) {
	ScopePerf perf(PE::DecodeTriggerEvent);

	// Timeout below approximately 12 rpm, but a maximum of 1 second timeout
	// Trigger shape length is ~4x tooth count (rise + fall / doubled for 4 stroke),
	// so extra multiply by 4 then 5 second maximum revolution
	float triggerTimeoutPeriod = clampF(0.1f, 20.0f / triggerShape.getLength(), 1.0f);
	if (previousEventTimer.getElapsedSecondsAndReset(nowNt) > triggerTimeoutPeriod) {
		/**
		 * We are here if there is a time gap between now and previous shaft event - that means the engine is not running.
		 * That means we have lost synchronization since the engine is not running :)
		 */
		setShaftSynchronized(false);
		if (triggerStateListener) {
			triggerStateListener->OnTriggerSynchronizationLost();
		}
	}

	bool useOnlyRisingEdgeForTrigger = triggerShape.useOnlyRisingEdges;

	TriggerWheel triggerWheel = eventIndex[(int)signal];
	bool isRising = eventType[(int)signal];

	// Check that we didn't get the same edge twice in a row - that should be impossible
	if (!useOnlyRisingEdgeForTrigger && prevSignal == signal) {
		orderingErrorCounter++;
	}

	prevSignal = signal;

	currentCycle.eventCount[(int)triggerWheel]++;

	logEdgeCounters(isRising);

	if (toothed_previous_time > nowNt) {
		firmwareError(ObdCode::CUSTOM_OBD_93, "[%s] toothed_previous_time after nowNt prev=%lu now=%lu", msg, (uint32_t)toothed_previous_time, (uint32_t)nowNt);
	}

	efidur_t currentDurationLong = isFirstEvent ? 0 : (nowNt - toothed_previous_time);

	/**
	 * For performance reasons, we want to work with 32 bit values. If there has been more then
	 * 10 seconds since previous trigger event we do not really care.
	 */
	toothDurations[0] =
			currentDurationLong > 10 * NT_PER_SECOND ? efidur_t{10 * NT_PER_SECOND} : currentDurationLong;

	if (!shouldConsiderEdge(triggerShape, triggerWheel, isRising)) {
#if EFI_UNIT_TEST
		if (printTriggerTrace) {
			printf("%s isLessImportant %s now=%d index=%d\r\n",
					getTrigger_type_e(triggerConfiguration.TriggerType.type),
					getTriggerEvent(signal),
					(int)nowNt,
					currentCycle.current_index);
		}
#endif /* EFI_UNIT_TEST */

		// For less important events we simply increment the index.
		nextTriggerEvent();
	} else {
#if !EFI_PROD_CODE
		if (printTriggerTrace) {
			printf("%s event %s %lld\r\n",
					getTrigger_type_e(triggerConfiguration.TriggerType.type),
					getTriggerEvent(signal),
					nowNt.count);
			printf("decodeTriggerEvent ratio %.2f: current=%d previous=%d\r\n", 1.0 * toothDurations[0] / toothDurations[1],
					toothDurations[0], toothDurations[1]);
		}
#endif

		isFirstEvent = false;
		bool isSynchronizationPoint;
		bool wasSynchronized = getShaftSynchronized();

		if (triggerShape.isSynchronizationNeeded) {
			triggerSyncGapRatio = (float)toothDurations[0] / toothDurations[1];

			isSynchronizationPoint = isSyncPoint(triggerShape, triggerConfiguration.TriggerType.type);
			if (isSynchronizationPoint) {
				enginePins.debugTriggerSync.toggle();
			}

			/**
			 * todo: technically we can afford detailed logging even with 60/2 as long as low RPM
			 * todo: figure out exact threshold as a function of RPM and tooth count?
			 * Open question what is 'triggerShape.getSize()' for 60/2 is it 58 or 58*2 or 58*4?
			 */
			bool silentTriggerError = triggerShape.getSize() > 40 && engineConfiguration->silentTriggerError;

#if EFI_PROD_CODE || EFI_SIMULATOR
			bool verbose = getTriggerCentral()->isEngineSnifferEnabled && triggerConfiguration.VerboseTriggerSynchDetails;

			if (verbose || (someSortOfTriggerError() && !silentTriggerError)) {
			    const char * prefix = verbose ? "[vrb]" : "[err]";

				for (int i = 0; i < triggerShape.gapTrackingLength; i++) {
					float ratioFrom = triggerShape.syncronizationRatioFrom[i];
					if (std::isnan(ratioFrom)) {
						// we do not track gap at this depth
						continue;
					}

					float gap = 1.0 * toothDurations[i] / toothDurations[i + 1];
					if (std::isnan(gap)) {
						efiPrintf("%s index=%d NaN gap, you have noise issues?", prefix, i);
					} else {
						float ratioTo = triggerShape.syncronizationRatioTo[i];

						bool gapOk = isInRange(ratioFrom, gap, ratioTo);

						efiPrintf("%s %srpm=%d time=%d eventIndex=%lu gapIndex=%d: %s gap=%.3f expected from %.3f to %.3f error=%s",
								prefix,
								triggerConfiguration.PrintPrefix,
								(int)Sensor::getOrZero(SensorType::Rpm),
							/* cast is needed to make sure we do not put 64 bit value to stack*/ (int)getTimeNowS(),
							currentCycle.current_index,
							i,
							gapOk ? "Y" : "n",
							gap,
							ratioFrom,
							ratioTo,
							boolToString(someSortOfTriggerError()));
					}
				}
			}
#else
			if (printTriggerTrace) {
				for (int i = 0; i < triggerShape.gapTrackingLength; i++) {
					float gap = 1.0 * toothDurations[i] / toothDurations[i + 1];
					printf("%sindex=%d: gap=%.2f expected from %.2f to %.2f error=%s\r\n",
							triggerConfiguration.PrintPrefix,
							i,
							gap,
							triggerShape.syncronizationRatioFrom[i],
							triggerShape.syncronizationRatioTo[i],
							boolToString(someSortOfTriggerError()));
				}
			}
#endif /* EFI_PROD_CODE */
		} else {
			/**
			 * We are here in case of a wheel without synchronization - we just need to count events,
			 * synchronization point simply happens once we have the right number of events
			 *
			 * in case of noise the counter could be above the expected number of events, that's why 'more or equals' and not just 'equals'
			 */

			unsigned int endOfCycleIndex = triggerShape.getSize() - (useOnlyRisingEdgeForTrigger ? 2 : 1);

			isSynchronizationPoint = !getShaftSynchronized() || (currentCycle.current_index >= endOfCycleIndex);

#if EFI_UNIT_TEST
			if (printTriggerTrace) {
				printf("decodeTriggerEvent sync=%d isSynchronizationPoint=%d index=%d size=%d\r\n",
						getShaftSynchronized(),
					isSynchronizationPoint,
					currentCycle.current_index,
					triggerShape.getSize());
			}
#endif /* EFI_UNIT_TEST */
		}
#if EFI_UNIT_TEST
		if (printTriggerTrace) {
			printf("decodeTriggerEvent %s isSynchronizationPoint=%d index=%d %s\r\n",
					getTrigger_type_e(triggerConfiguration.TriggerType.type),
					isSynchronizationPoint, currentCycle.current_index,
					getTriggerEvent(signal));
		}
#endif /* EFI_UNIT_TEST */

		if (isSynchronizationPoint) {
			bool isDecodingError = validateEventCounters(triggerShape);

			if (triggerStateListener) {
				triggerStateListener->OnTriggerSyncronization(wasSynchronized, isDecodingError);
			}

			// If we got a sync point, but the wrong number of events since the last sync point
			// One of two things has happened:
			//  - We missed a tooth, and this is the real sync point
			//  - Due to some mistake in timing, we found what looks like a sync point but actually isn't
			// In either case, we should wait for another sync point before doing anything to try and run an engine,
			// so we clear the synchronized flag.
			if (wasSynchronized && isDecodingError) {
				setTriggerErrorState();
				onNotEnoughTeeth(currentCycle.current_index, triggerShape.getSize());

				// Something wrong, no longer synchronized
				setShaftSynchronized(false);

				// This is a decoding error
				onTriggerError();
			} else {
				// If this was the first sync point OR no decode error, we're synchronized!
				setShaftSynchronized(true);
			}

			// this call would update duty cycle values
			nextTriggerEvent();

			onShaftSynchronization(wasSynchronized, nowNt, triggerShape);
		} else {
			// If not the sync point but we are synchronized, just increment tooth index.
			if (getShaftSynchronized()) {
				nextTriggerEvent();
			}
		}

		for (int i = triggerShape.gapTrackingLength; i > 0; i--) {
			toothDurations[i] = toothDurations[i - 1];
		}

		toothed_previous_time = nowNt;

#if EFI_UNIT_TEST
        if (wasSynchronized) {
            int uiGapIndex = (currentCycle.current_index) % triggerShape.getLength();
            gapRatio[uiGapIndex] = triggerSyncGapRatio;
        }
#endif // EFI_UNIT_TEST
	}

	if (getShaftSynchronized() && !isValidIndex(triggerShape)) {
		// We've had too many events since the last sync point, we should have seen a sync point by now.
		// This is a trigger error.

		// let's not show a warning if we are just starting to spin
		if (Sensor::getOrZero(SensorType::Rpm) != 0) {
			setTriggerErrorState();
			onTooManyTeeth(currentCycle.current_index, triggerShape.getSize());
		}

		onTriggerError();

		setShaftSynchronized(false);

		return unexpected;
	}

	// Needed for early instant-RPM detection
	if (triggerStateListener) {
		triggerStateListener->OnTriggerStateProperState(nowNt);
	}

	triggerStateIndex = currentCycle.current_index;

	if (getShaftSynchronized()) {
		return TriggerDecodeResult{ currentCycle.current_index };
	} else {
		return unexpected;
	}
}

bool TriggerDecoderBase::isSyncPoint(const TriggerWaveform& triggerShape, trigger_type_e triggerType) {
	// Miata NB needs a special decoder.
	// The problem is that the crank wheel only has 4 teeth, also symmetrical, so the pattern
	// is long-short-long-short for one crank rotation.
	// A quick acceleration can result in two successive "short gaps", so we see 
	// long-short-short-short-long instead of the correct long-short-long-short-long
	// This logic expands the lower bound on a "long" tooth, then compares the last
	// tooth to the current one.

	// Instead of detecting short/long, this logic first checks for "maybe short" and "maybe long",
	// then simply tests longer vs. shorter instead of absolute value.
	if (triggerType == trigger_type_e::TT_MIATA_VVT) {
		bool useToothCounting = getShaftSynchronized() && (Sensor::getOrZero(SensorType::Rpm) < 1000);

		// If we got reasonable sync and we're spinning slowly, just count teeth. This is necessary
		// because a large jump in speed can cause the "long gap" to actually be shorter than the previous
		// "short gap", but the engine sped up a bunch.
		if (useToothCounting) {
			// If we're an NB and have a reasonable gap, let's just say we're still synchronized
			if (isInRange(0.4f, (float)triggerSyncGapRatio, 2.0f)) {
				// If the last tooth wasn't a sync point, this one is
				return getCurrentIndex() != 0;
			} else {
				setShaftSynchronized(false);
				return false;
			}
		} else {
			auto secondGap = (float)toothDurations[1] / toothDurations[2];

			bool currentGapOk = isInRange(triggerShape.syncronizationRatioFrom[0], (float)triggerSyncGapRatio, triggerShape.syncronizationRatioTo[0]);
			bool secondGapOk  = isInRange(triggerShape.syncronizationRatioFrom[1], secondGap,  triggerShape.syncronizationRatioTo[1]);

			// One or both teeth was impossible range, this is not the sync point
			if (!currentGapOk || !secondGapOk) {
				return false;
			}

			// If both teeth are in the range of possibility, return whether this gap is
			// shorter than the last or not.  If it is, this is the sync point.
			return triggerSyncGapRatio < secondGap;
		}
	}

	for (int i = 0; i < triggerShape.gapTrackingLength; i++) {
		auto from = triggerShape.syncronizationRatioFrom[i];
		auto to = triggerShape.syncronizationRatioTo[i];

		if (std::isnan(from)) {
			// don't check this gap, skip it
			continue;
		}

		// This is transformed to avoid a division and use a cheaper multiply instead
		// toothDurations[i] / toothDurations[i+1] > from
		// is an equivalent comparison to
		// toothDurations[i] > toothDurations[i+1] * from
		bool isGapCondition = 
			  (toothDurations[i] > toothDurations[i + 1] * from
			&& toothDurations[i] < toothDurations[i + 1] * to);

		if (!isGapCondition) {
			return false;
		}
	}

	return true;
}

/**
 * Trigger shape is defined in a way which is convenient for trigger shape definition
 * On the other hand, trigger decoder indexing begins from synchronization event.
 *
 * This function finds the index of synchronization event within TriggerWaveform
 */
expected<uint32_t> TriggerDecoderBase::findTriggerZeroEventIndex(
		TriggerWaveform& shape,
		const TriggerConfiguration& triggerConfiguration) {
	resetState();

	if (shape.shapeDefinitionError) {
		return unexpected;
	}

	expected<uint32_t> syncIndex = TriggerStimulatorHelper::findTriggerSyncPoint(shape,
			triggerConfiguration,
			*this);
	if (!syncIndex) {
		return unexpected;
	}

	// Assert that we found the sync point on the very first revolution
	efiAssert(ObdCode::CUSTOM_ERR_ASSERT, getCrankSynchronizationCounter() == 0, "findZero_revCounter", unexpected);

#if EFI_UNIT_TEST
	if (printTriggerDebug) {
		printf("findTriggerZeroEventIndex: syncIndex located %d!\r\n", syncIndex);
	}
#endif /* EFI_UNIT_TEST */

	TriggerStimulatorHelper::assertSyncPosition(triggerConfiguration,
			syncIndex.Value, *this, shape);

	return syncIndex.Value % shape.getSize();
}

#endif /* EFI_SHAFT_POSITION_INPUT */

