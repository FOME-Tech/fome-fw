/**
 * @file fuel_schedule.h
 */

#pragma once

#include "global.h"
#include "efi_gpio.h"
#include "scheduler.h"
#include "trigger_structure.h"
#include "wall_fuel.h"
#include "cycle_event_tracker.h"

#define MAX_WIRES_COUNT 2

class InjectionEvent {
public:
	// Choose the injection mode for the next pulse. Returns false if injections can't be placed yet.
	bool update();

	// FuelSchedule::onTriggerTooth calls these on every decoded trigger tooth. pollDue says whether this
	// event's next pulse has to start before the next tooth. If it does, the instance must then be passed to
	// skipIfNotToInject and, if that didn't skip it, to injectInstance, which schedules it. If that fails,
	// the instance has been skipped, and the caller must call update() once it has put together every pulse
	// on this tooth.
	expected<CycleEventInstance> pollDue(const EnginePhaseInfo& phase, float leadFraction, floatus_t oneDegreeUs);

#if EFI_UNIT_TEST
	// For tests: what FuelSchedule::onTriggerTooth does, for this event on its own and without waiting for
	// the schedule to be ready
	void onTriggerTooth(const EnginePhaseInfo& phase);
#endif

	bool skipIfNotToInject(const CycleEventInstance& instance, const EnginePhaseInfo& phase);
	bool injectInstance(
			const CycleEventInstance& instance,
			const EnginePhaseInfo& phase,
			float leadFraction,
			floatus_t oneDegreeUs);

	// Forget which cycles have had their pulse: engine stop or configuration change
	void resetTracker() {
		m_tracker.reset();
	}

	WallFuel& getWallFuel();

	void setIndex(uint8_t index) {
		ownIndex = index;
	}

	uint16_t calculateInjectorOutputMask() const;
	bool shouldFireInjectorAtIndex(size_t otherIndex) const;

	// Work out how long this event's next pulse would be if it were scheduled now: this cylinder's fuel,
	// this event's own injection mode, wall wetting as it stands, staging. Called at the end of the fast
	// callback, once everything that goes in to it is up to date, so it matches what is actually injected
	// unless something changes in between.
	void updateDurationEstimate();

	// Stage 1 duration, in ms, from the last updateDurationEstimate()
	floatms_t getDurationEstimate() const {
		return m_durationEstimateMs;
	}

private:
	// Work out and schedule the pulse for an instance. Returns false if there's nothing to inject, or it
	// couldn't be scheduled.
	bool scheduleInstance(
			const CycleEventInstance& instance,
			const EnginePhaseInfo& phase,
			float leadFraction,
			floatus_t oneDegreeUs);
	void skipInstance(const CycleEventInstance& instance, const EnginePhaseInfo& phase);

	// Which cycles have had their pulse
	CycleEventTracker m_tracker;

	floatms_t m_durationEstimateMs = 0;
	uint8_t ownIndex = 0;
	uint8_t cylinderNumber = 0;
	injection_mode_e m_injectionMode = IM_SEQUENTIAL;

	WallFuel wallFuel;
};

// How much of a pulse comes before its anchor, the angle the injection phase table sets: all of it when the
// table sets the end of injection, half for the middle, none for the start
float getInjectionLeadFraction();

union InjectorContext {
	constexpr InjectorContext() {
		// First, initialize all bits to a preditable state
		_pad = nullptr;

		// Then initialize real values
		outputsMask = 0;
		eventIndex = 0xF;
		splitDurationUs = 0;
		stage2Active = false;
	}

	struct {
		uint16_t outputsMask : 12;
		uint8_t eventIndex : 4;
		uint16_t splitDurationUs : 15;
		bool stage2Active : 1;
	};
	void* _pad;
};

static_assert(sizeof(InjectorContext) <= sizeof(void*));

void startInjection(InjectorContext ctx);
void endInjection(InjectorContext ctx);
void endInjectionStage2(InjectorContext ctx);

/**
 * This class knows about when to inject fuel
 */
class FuelSchedule {
public:
	FuelSchedule();

	// Call this function if something happens that requires a rebuild, like a change to the trigger pattern.
	// Each event chooses its injection mode again.
	void invalidate();

	// Forget which cycles have had their pulse: engine stop or configuration change. Not for losing trigger
	// sync, which leaves that history valid.
	void resetTrackers();

	// Call this every trigger tooth.  It will schedule all required injector events.
	void onTriggerTooth(const EnginePhaseInfo& phase);

	// Calculate injector opening angle, pins, and mode for all injectors
	void addFuelEvents();

	// See InjectionEvent::updateDurationEstimate
	void updateDurationEstimates();

	/**
	 * injection events, per cylinder
	 */
	InjectionEvent elements[MAX_CYLINDER_COUNT];
	bool isReady = false;
};

FuelSchedule* getFuelSchedule();
