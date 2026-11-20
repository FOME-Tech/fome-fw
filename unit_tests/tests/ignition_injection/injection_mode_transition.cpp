/*
 * @file injection_mode_transition.cpp
 *
 *  Created on: Jul 19, 2020
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"

#include <map>

using ::testing::_;

static void doRevolution(EngineTestHelper& eth, int periodMs) {
	float halfToothTime = (periodMs / 6.0f) / 2;

	eth.smartFireRise(halfToothTime);
	eth.fireFall(halfToothTime);
	eth.smartFireRise(halfToothTime);
	eth.fireFall(halfToothTime);
	eth.smartFireRise(halfToothTime);
	eth.fireFall(halfToothTime);

	// now missing tooth
	eth.smartFireRise(halfToothTime);
	eth.fireFall(3 * halfToothTime);

	// This tooth is the sync point!
	eth.smartFireRise(halfToothTime);
	eth.fireFall(halfToothTime);
}

// https://github.com/rusefi/rusefi/issues/1592
TEST(fuelControl, transitionIssue1592) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engine->tdcMarkEnabled = false;
	setupSimpleTestEngineWithMafAndTT_ONE_trigger(&eth, IM_SEQUENTIAL);

	EXPECT_CALL(*eth.mockAirmass, getAirmass(500, _)).WillRepeatedly(Return(AirmassResult{0.1008f, 50.0f}));

	// This is easiest to trip on a wheel that requires sync
	engineConfiguration->trigger.customTotalToothCount = 6;
	engineConfiguration->trigger.customSkippedToothCount = 1;
	eth.setTriggerType(trigger_type_e::TT_TOOTHED_WHEEL);
	setCamOperationMode();
	engineConfiguration->isFasterEngineSpinUpEnabled = true;

	setTable(config->injectionPhase, 0.0f);
	setArrayValues(config->crankingFuelCoef, 1.0f);
	setArrayValues(config->crankingCycleCoef, 1.0f);

	engineConfiguration->globalTriggerAngleOffset = 20;

	// Yes, this is a ton of fuel but it makes the repro easier
	engineConfiguration->cranking.baseFuel = 213.6;
	engineConfiguration->cranking.rpm = 501;

	// Test the transition from batch cranking to sequential running
	engineConfiguration->crankingInjectionMode = IM_BATCH;

	// First sync point will schedule cranking pulse since we're in "faster spin up" mode
	doRevolution(eth, 240);

	{
		// Cranking pulses are scheduled, each open together with a close for the same injectors. (Which and
		// how many depends on where the first sync point lands: any whose start has already gone by opens
		// straight away.)
		ASSERT_GT(engine->scheduler.size(), 0);

		std::map<void*, int> openMinusClose;
		for (int i = 0; i < engine->scheduler.size(); i++) {
			auto scheduled = engine->scheduler.getForUnitTest(i);
			if (scheduled->action.getCallback() == (void (*)(void*))startInjection) {
				openMinusClose[scheduled->action.getArgument()]++;
			} else if (scheduled->action.getCallback() == (void (*)(void*))endInjection) {
				openMinusClose[scheduled->action.getArgument()]--;
			}
		}

		for (const auto& [ctx, balance] : openMinusClose) {
			EXPECT_EQ(0, balance) << "an open without its close, or the reverse";
		}
	}

	// Run the engine for some revs
	for (size_t i = 0; i < 10; i++) {
		doRevolution(eth, 150);
	}

	// Check that no injectors are stuck open: some are mid-pulse right now, but none has been opened more
	// times than it has been closed, beyond the pulse it's in
	for (size_t i = 0; i < 4; i++) {
		EXPECT_LE(enginePins.injectors[i].getOverlappingCounter(), 1) << "injector " << i;
	}

	// Trigger signal disappears but time progresses, all injectors should close
	eth.moveTimeForwardAndInvokeEventsUs(1e6);

	EXPECT_EQ(enginePins.injectors[0].getOverlappingCounter(), 0);
	EXPECT_EQ(enginePins.injectors[1].getOverlappingCounter(), 0);
	EXPECT_EQ(enginePins.injectors[2].getOverlappingCounter(), 0);
	EXPECT_EQ(enginePins.injectors[3].getOverlappingCounter(), 0);
}
