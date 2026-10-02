/*
 * test_fasterEngineSpinningUp.cpp
 *
 *  Created on: Mar 6, 2018
 */

#include "pch.h"
#include "main_trigger_callback.h"
#include "spark_logic.h"

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
	eth.assertEvent5("inj start#1", 0, (void*)scheduledStartInjection, 97500);
	eth.assertEvent5("inj end#1", 1, (void*)scheduledEndInjection, 100000);

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
	eth.assertEvent5("inj start#2", 0, (void*)scheduledStartInjection, 148375);
	eth.assertEvent5("inj end#2", 1, (void*)scheduledEndInjection, 149999);

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
	eth.assertEvent5("inj start#3", 0, (void*)scheduledStartInjection, -30326);
	eth.assertEvent5("inj end#3", 1, (void*)scheduledEndInjection, -28702);
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

namespace {
void configureSynchronousStartup(EngineTestHelper& eth) {
	setupSimpleTestEngineWithMafAndTT_ONE_trigger(&eth);
	engineConfiguration->isFasterEngineSpinUpEnabled = true;
	engineConfiguration->fuelAlgorithm = LM_SD_ALPHA_N;
	engineConfiguration->isInjectionEnabled = true;
	engineConfiguration->isIgnitionEnabled = true;
	engineConfiguration->cranking.rpm = 999;
	engineConfiguration->useSeparateVeForIdle = false;
	engineConfiguration->alphaNUseIat = true;
	engineConfiguration->useSeparateAdvanceForCranking = true;
	engineConfiguration->ignitionDwellForCrankingMs = 4;
	engineConfiguration->useAdvanceCorrectionsForCranking = false;
	setTable(config->veTable, 60);
	setTable(config->alphaNTable, 40);
	setTable(config->airmassBlendTable, 0);
	setTable(config->lambdaTable, 1);
	setTable(config->injectionPhase, -180);
	setArrayValues(config->crankingAdvance, 7);
	Sensor::setMockValue(SensorType::Map, 80);
	Sensor::setMockValue(SensorType::Tps1, 20);
	Sensor::setMockValue(SensorType::DriverThrottleIntent, 20);
	Sensor::setMockValue(SensorType::Iat, 20);
	Sensor::setMockValue(SensorType::Clt, 80);
	engine->ignitionState.sparkDwell = -1;
	engine->ignitionState.dwellAngle = -1;
	for (auto& cylinder : engine->cylinders) {
		cylinder.setIgnitionTimingBtdc(-123);
	}
}

void reachFirstPositiveRpm(EngineTestHelper& eth) {
	eth.fireRise(1000);
	ASSERT_EQ(0, Sensor::getOrZero(SensorType::Rpm));
	eth.fireRise(200);
	ASSERT_GT(Sensor::getOrZero(SensorType::Rpm), 0);
}

size_t countStartupCallbacks(schfunc_t callback) {
	size_t count = 0;
	for (int i = 0; i < engine->scheduler.size(); i++) {
		count += engine->scheduler.getForUnitTest(i)->action.getCallback() == callback;
	}
	return count;
}
} // namespace

TEST(SynchronousStartup, FirstPositiveRpmPreparesFuelSparkAndDefersIdleActuator) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureSynchronousStartup(eth);
	::testing::NiceMock<MockEtb> etb;
	engine->etbControllers[0] = &etb;
	EXPECT_CALL(etb, setIdlePosition(::testing::_)).Times(0);

	// No regular fast callback runs between the trigger events and these assertions.
	reachFirstPositiveRpm(eth);
	ASSERT_TRUE(engine->airmassInjectionState.allowInjection());
	ASSERT_GT(engine->cylinders[0].getInjectionMass(), 0);
	ASSERT_GT(engine->engineState.injectionDuration, 0);
	EXPECT_FLOAT_EQ(engine->ignitionState.sparkDwell, 4);
	EXPECT_GT(engine->ignitionState.dwellAngle, 0);
	EXPECT_FLOAT_EQ(engine->cylinders[0].getIgnitionTimingBtdc(), 7);
	EXPECT_TRUE(getLimpManager()->allowInjection().value);
	EXPECT_TRUE(getLimpManager()->allowIgnition().value);
	EXPECT_GT(countStartupCallbacks((schfunc_t)scheduledStartInjection), 0u);
	EXPECT_TRUE(engine->ignitionEvents.isReady);
	EXPECT_GT(countStartupCallbacks((schfunc_t)turnSparkPinHigh), 0u);

	::testing::Mock::VerifyAndClearExpectations(&etb);
	EXPECT_CALL(etb, setIdlePosition(::testing::_)).Times(1);
	engine->periodicFastCallback();
	engine->etbControllers[0] = nullptr;
}

TEST(SynchronousStartup, FirstPositiveRpmAppliesFloodClearAndLuaSparkCut) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureSynchronousStartup(eth);
	engineConfiguration->isCylinderCleanupEnabled = true;
	Sensor::setMockValue(SensorType::DriverThrottleIntent, 100);
	engine->engineState.lua.luaIgnCut = true;
	reachFirstPositiveRpm(eth);
	EXPECT_FALSE(getLimpManager()->allowInjection().value);
	EXPECT_EQ(getLimpManager()->allowInjection().reason, ClearReason::FloodClear);
	EXPECT_FALSE(getLimpManager()->allowIgnition().value);
	EXPECT_EQ(getLimpManager()->allowIgnition().reason, ClearReason::Lua);
	EXPECT_EQ(countStartupCallbacks((schfunc_t)scheduledStartInjection), 0u);
	EXPECT_EQ(countStartupCallbacks((schfunc_t)turnSparkPinHigh), 0u);
}

TEST(SynchronousStartup, DisabledOutputsClearStaleFuelDwellAndAdvance) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureSynchronousStartup(eth);
	engineConfiguration->isInjectionEnabled = false;
	engineConfiguration->isIgnitionEnabled = false;
	engine->cylinders[0].setInjectionMass(1);
	engine->engineState.injectionDuration = 9;
	reachFirstPositiveRpm(eth);
	EXPECT_FLOAT_EQ(engine->cylinders[0].getInjectionMass(), 0);
	EXPECT_FLOAT_EQ(engine->engineState.injectionDuration, 0);
	EXPECT_FLOAT_EQ(engine->ignitionState.sparkDwell, 0);
	EXPECT_FLOAT_EQ(engine->ignitionState.dwellAngle, 0);
	EXPECT_FLOAT_EQ(engine->cylinders[0].getIgnitionTimingBtdc(), 0);
	EXPECT_FALSE(getLimpManager()->allowInjection().value);
	EXPECT_FALSE(getLimpManager()->allowIgnition().value);
	EXPECT_EQ(countStartupCallbacks((schfunc_t)scheduledStartInjection), 0u);
}

TEST(SynchronousStartup, FirstPositiveRpmAppliesHardLimit) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureSynchronousStartup(eth);
	engineConfiguration->rpmHardLimit = 100;
	engineConfiguration->cutFuelOnHardLimit = true;
	engineConfiguration->cutSparkOnHardLimit = true;
	reachFirstPositiveRpm(eth);
	EXPECT_FALSE(getLimpManager()->allowInjection().value);
	EXPECT_FALSE(getLimpManager()->allowIgnition().value);
	EXPECT_EQ(getLimpManager()->allowInjection().reason, ClearReason::HardLimit);
	EXPECT_EQ(getLimpManager()->allowIgnition().reason, ClearReason::HardLimit);
	EXPECT_EQ(countStartupCallbacks((schfunc_t)scheduledStartInjection), 0u);
	EXPECT_EQ(countStartupCallbacks((schfunc_t)turnSparkPinHigh), 0u);
}

TEST(SynchronousStartup, TriggerConfigurationChangeRefreshesCoreWithoutIdleActuator) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureSynchronousStartup(eth);
	reachFirstPositiveRpm(eth);
	eth.moveTimeForwardAndInvokeEventsUs(200000);
	::testing::NiceMock<MockEtb> etb;
	engine->etbControllers[0] = &etb;
	EXPECT_CALL(etb, setIdlePosition(::testing::_)).Times(0);
	setArrayValues(config->crankingAdvance, 13);
	engineConfiguration->ignitionDwellForCrankingMs = 5;
	engineConfiguration->globalTriggerAngleOffset += 1;
	incrementGlobalConfigurationVersion();
	ASSERT_TRUE(engine->triggerCentral.isTriggerConfigChanged());

	mainTriggerCallback(0, {getTimeNowNt(), 0, 0, 0, 360});
	EXPECT_FALSE(engine->triggerCentral.isTriggerConfigChanged());
	EXPECT_TRUE(engine->airmassInjectionState.allowInjection());
	EXPECT_GT(engine->cylinders[0].getInjectionMass(), 0);
	EXPECT_FLOAT_EQ(engine->ignitionState.sparkDwell, 5);
	EXPECT_FLOAT_EQ(engine->cylinders[0].getIgnitionTimingBtdc(), 13);
	EXPECT_TRUE(engine->ignitionEvents.isReady);
	engine->etbControllers[0] = nullptr;
}

TEST(SynchronousStartup, UnreviewedModuleRetainsFastPreparation) {
	struct NewModule : EngineModule {
		void onFastCallback() override {
			++calls;
		}
		int calls = 0;
	} module;
	module.onSynchronousFastCallback();
	EXPECT_EQ(module.calls, 1);
}
