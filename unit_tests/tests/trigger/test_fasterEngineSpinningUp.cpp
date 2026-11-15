/*
 * test_fasterEngineSpinningUp.cpp
 *
 *  Created on: Mar 6, 2018
 */

#include "pch.h"

TEST(cranking, testFasterEngineSpinningUp) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setTable(config->injectionPhase, -180.0f);
	engine->tdcMarkEnabled = false;
	// turn on FasterEngineSpinUp mode
	engineConfiguration->isFasterEngineSpinUpEnabled = true;
	engineConfiguration->cranking.baseFuel = 12;

	// set ignition mode
	engineConfiguration->ignitionMode = IM_INDIVIDUAL_COILS;
	// set cranking threshold (used below)
	engineConfiguration->cranking.rpm = 999;
	// set sequential injection mode to test auto-change to simultaneous when spinning-up
	setupSimpleTestEngineWithMafAndTT_ONE_trigger(&eth, IM_SEQUENTIAL);
	// Lie that this trigger requires disambiguation
	engine->triggerCentral.triggerState.setNeedsDisambiguation(true, true);

	ASSERT_EQ(IM_WASTED_SPARK, getCurrentIgnitionMode());

	eth.fireRise(1000 /*ms*/);

	// Until we get cam sync, we should be in batch fuel/wasted spark
	ASSERT_EQ(IM_BATCH, getCurrentInjectionMode());
	ASSERT_EQ(IM_WASTED_SPARK, getCurrentIgnitionMode());
	// check if the engine has the right state
	ASSERT_EQ(SPINNING_UP, engine->rpmCalculator.getState());
	// check RPM
	ASSERT_EQ(0, round(Sensor::getOrZero(SensorType::Rpm))) << "RPM=0";
	// the queue should be empty, no trigger events yet
	ASSERT_EQ(0, engine->scheduler.size()) << "plain#1";

	// check all events starting from now
	// advance 1 revolution
	// because we have trivial trigger_type_e::TT_ONE trigger here synchronization would happen with just one rise front
	eth.fireRise(200);

	// check if the mode is changed
	ASSERT_EQ(SPINNING_UP, engine->rpmCalculator.getState());
	// due to isFasterEngineSpinUp=true, we should have already detected RPM!
	ASSERT_EQ(300, round(Sensor::getOrZero(SensorType::Rpm))) << "spinning-RPM#1";
	// two simultaneous injections
	ASSERT_EQ(4, engine->scheduler.size()) << "plain#2";
	// test if they are simultaneous
	ASSERT_EQ(IM_SIMULTANEOUS, getCurrentInjectionMode());
	// test if ignition mode is temporary changed to wasted spark, if set to individual coils
	ASSERT_EQ(IM_WASTED_SPARK, getCurrentIgnitionMode());
	// check real events
	eth.assertEvent5("inj start#1", 0, (void*)startInjection, 97500);
	eth.assertEvent5("inj end#1", 1, (void*)endInjection, 100000);

	// skip the rest of the cycle
	eth.moveTimeForwardUs(MS2US(200));

	// now clear and advance more
	eth.executeActions();

	eth.fireRise(200);

	// check if the mode is changed when fully synched
	ASSERT_EQ(CRANKING, engine->rpmCalculator.getState());
	// check RPM
	ASSERT_EQ(200, round(Sensor::getOrZero(SensorType::Rpm))) << "RPM#2";
	// test if they are simultaneous in cranking mode too
	ASSERT_EQ(IM_SIMULTANEOUS, getCurrentInjectionMode());
	// Should still be in wasted spark since we don't have cam sync yet
	ASSERT_EQ(IM_WASTED_SPARK, getCurrentIgnitionMode());
	// two simultaneous injections
	ASSERT_EQ(4, engine->scheduler.size()) << "plain#2";
	// check real events
	eth.assertEvent5("inj start#2", 0, (void*)startInjection, 148375);
	eth.assertEvent5("inj end#2", 1, (void*)endInjection, 149999);

	// Now perform a fake VVT sync and check that ignition mode changes to sequential
	engine->triggerCentral.syncAndReport(2, 0);
	ASSERT_EQ(IM_SEQUENTIAL, getCurrentIgnitionMode());

	// skip, clear & advance 1 more revolution at higher RPM
	eth.fireFall(60);

	eth.clearQueue();
	eth.fireTriggerEventsWithDuration(60);

	// check if the mode is now changed to 'running' at higher RPM
	ASSERT_EQ(RUNNING, engine->rpmCalculator.getState());
	// check RPM
	ASSERT_EQ(1000, round(Sensor::getOrZero(SensorType::Rpm))) << "RPM#3";
	// check if the injection mode is back to sequential now
	ASSERT_EQ(IM_SEQUENTIAL, getCurrentInjectionMode());
	// 4 sequential injections for the full cycle
	ASSERT_EQ(8, engine->scheduler.size()) << "plain#3";

	// check real events for sequential injection
	// Note: See addFuelEvents() fix inside setRpmValue()!
	eth.assertEvent5("inj start#3", 0, (void*)startInjection, -30326);
	eth.assertEvent5("inj end#3", 1, (void*)endInjection, -28702);
}

static void setupSequentialCranking(EngineTestHelper& eth, bool expectCamSync) {
	setTable(config->injectionPhase, -180.0f);
	engine->tdcMarkEnabled = false;
	engineConfiguration->isFasterEngineSpinUpEnabled = true;
	engineConfiguration->cranking.baseFuel = 12;
	// stay in cranking for the whole test
	engineConfiguration->cranking.rpm = 999;

	setupSimpleTestEngineWithMafAndTT_ONE_trigger(&eth, IM_SEQUENTIAL);
	engineConfiguration->crankingInjectionMode = IM_SEQUENTIAL;

	// Crank-only wheel that needs a cam for phase
	engine->triggerCentral.triggerState.setNeedsDisambiguation(true, expectCamSync);

	eth.fireRise(1000 /*ms*/);
	// Faster spin up gives us RPM on the second tooth, which counts as cranking
	eth.fireRise(200);
	ASSERT_TRUE(engine->rpmCalculator.isCranking());
}

// Sequential cranking before the cam has synced would time pulses off a guessed engine phase,
// which is wrong by 360 degrees about half the time. Until phase is known, crank in batch instead.
TEST(cranking, sequentialCrankingUsesBatchUntilPhaseSync) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setupSequentialCranking(eth, /*expectCamSync=*/true);

	EXPECT_EQ(IM_BATCH, getCurrentInjectionMode());

	// Every cranking pulse scheduled so far fires a 360 degree pair of injectors
	ASSERT_GT(engine->scheduler.size(), 0);
	for (int i = 0; i < engine->scheduler.size(); i++) {
		auto ctx = bit_cast<InjectorContext>(engine->scheduler.getForUnitTest(i)->action.getArgument());
		EXPECT_EQ(2, __builtin_popcount(ctx.outputsMask)) << "scheduled event " << i;
	}

	// Cam syncs while still cranking: switch to sequential right away
	engine->triggerCentral.syncAndReport(2, 0);
	ASSERT_TRUE(engine->rpmCalculator.isCranking());
	EXPECT_EQ(IM_SEQUENTIAL, getCurrentInjectionMode());
}

// With no cam sync on the way, waiting for phase would mean batch forever, so honor sequential as
// configured (same rule as running mode)
TEST(cranking, sequentialCrankingWithoutCamStaysSequential) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setupSequentialCranking(eth, /*expectCamSync=*/false);

	EXPECT_EQ(IM_SEQUENTIAL, getCurrentInjectionMode());
}

// Non-sequential cranking modes are not affected by phase sync
TEST(cranking, nonSequentialCrankingModeUnchangedBeforePhaseSync) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setupSequentialCranking(eth, /*expectCamSync=*/true);

	engineConfiguration->crankingInjectionMode = IM_SIMULTANEOUS;
	EXPECT_EQ(IM_SIMULTANEOUS, getCurrentInjectionMode());

	engineConfiguration->crankingInjectionMode = IM_BATCH;
	EXPECT_EQ(IM_BATCH, getCurrentInjectionMode());
}

static void doTestFasterEngineSpinningUp60_2(int startUpDelayMs, int rpm1, int expectedRpm) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	// turn on FasterEngineSpinUp mode
	engineConfiguration->isFasterEngineSpinUpEnabled = true;

	setupSimpleTestEngineWithMaf(&eth, IM_SEQUENTIAL, trigger_type_e::TT_TOOTHED_WHEEL_60_2);
	eth.moveTimeForwardMs(startUpDelayMs);

	// fire 30 tooth rise/fall signals
	eth.fireTriggerEvents2(30 /* count */, 1 /*ms*/);
	// now fire missed tooth rise/fall
	eth.fireRise(5 /*ms*/);
	EXPECT_EQ(rpm1, round(Sensor::getOrZero(SensorType::Rpm)));

	eth.fireFall(1);
	eth.fireTriggerEvents2(30, 1);

	// After some more regular teeth, instant RPM is still correct
	EXPECT_EQ(rpm1, round(Sensor::getOrZero(SensorType::Rpm)));
}

TEST(cranking, testFasterEngineSpinningUp60_2) {
	doTestFasterEngineSpinningUp60_2(0, 1000, 1000);
	doTestFasterEngineSpinningUp60_2(100, 1000, 1000);
	doTestFasterEngineSpinningUp60_2(1000, 1000, 1000);
}
