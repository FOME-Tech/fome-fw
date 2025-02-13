/**
 * @file fuel_schedule.h
 */

#pragma once

#include "global.h"
#include "efi_gpio.h"
#include "scheduler.h"
#include "fl_stack.h"
#include "trigger_structure.h"
#include "wall_fuel.h"

#define MAX_WIRES_COUNT 2

class InjectionEvent {
public:
	InjectionEvent();

	bool update();

	// Call this every decoded trigger tooth.  It will schedule any relevant events for this injector.
	void onTriggerTooth(efitick_t nowNt, float currentPhase, float nextPhase);

	WallFuel& getWallFuel();

	void setIndex(uint8_t index) {
		ownIndex = index;
	}

private:
	// Update the injection start angle
	bool updateInjectionAngle(injection_mode_e mode);

	/**
	 * This is a performance optimization for IM_SIMULTANEOUS fuel strategy.
	 * It's more efficient to handle all injectors together if that's the case
	 */
	bool isSimultaneous = false;
	uint8_t ownIndex = 0;
	uint8_t cylinderNumber = 0;
	injection_mode_e m_injectionMode = IM_SEQUENTIAL;

	WallFuel wallFuel;

public:
	// TODO: this should be private
	uint16_t outputsMask = 0;
	float injectionStartAngle = 0;
};

union InjectorContext {
	constexpr InjectorContext() { }

	struct {
		uint16_t outputsMask:12 = 0;
		uint8_t eventIndex:4 = 0xF;
		uint16_t splitDurationUs:15 = 0;
		bool stage2Active:1 = false;
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

	// Call this function if something happens that requires a rebuild, like a change to the trigger pattern
	void invalidate();

	// Call this every trigger tooth.  It will schedule all required injector events.
	void onTriggerTooth(efitick_t nowNt, float currentPhase, float nextPhase);

	// Calculate injector opening angle, pins, and mode for all injectors
	void addFuelEvents();

	/**
	 * injection events, per cylinder
	 */
	InjectionEvent elements[MAX_CYLINDER_COUNT];
	bool isReady = false;
};

FuelSchedule * getFuelSchedule();
