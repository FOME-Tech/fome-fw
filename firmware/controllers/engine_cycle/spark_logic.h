/*
 * @file spark_logic.h
 *
 * @date Sep 15, 2016
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#pragma once

void onTriggerEventSparkLogic(efitick_t edgeTimestamp, float currentPhase, float nextPhase);
int getNumberOfSparks(ignition_mode_e mode);
percent_t getCoilDutyCycle(float rpm);
void initializeIgnitionActions();

union IgnitionContext {
	constexpr IgnitionContext() {
		// First, initialize all bits to a preditable state
		_pad = nullptr;

		// Then initialize real values
		outputsMask = 0;
		eventIndex = 0xF;
		sparksRemaining = 0;
		isOverdwellProtect = false;
	}

	struct {
		uint16_t outputsMask:12;
		uint8_t eventIndex:4;

		// How many additional sparks should we fire after the first one?
		// For single sparks, this should be zero.
		uint8_t sparksRemaining;

		bool isOverdwellProtect:1;
	};
	void* _pad;
};

static_assert(sizeof(IgnitionContext) <= sizeof(void*));

void turnSparkPinHigh(IgnitionContext ctx);
void fireSparkAndPrepareNextSchedule(IgnitionContext ctx);
