/*
 * @file test_hpfp_integrated.cpp
 *
 *  Created on: Jan 18, 2022
 * More integrated version of HPFP test
 */

#include "pch.h"
#include "high_pressure_fuel_pump.h"

namespace {

void verifyTriggerConfigChangeWithPendingHpfpTimer(bool closing) {
	AngleBasedEvent obsolete;
	EngineTestHelper eth(engine_type_e::TEST_ENGINE, [](engine_configuration_s* cfg) {
		cfg->hpfpValvePin = Gpio::A2;
		cfg->hpfpCamLobes = 3;
		cfg->hpfpPumpVolume = 0.2;
		cfg->hpfpActivationAngle = 30;
		cfg->isIgnitionEnabled = false;
		cfg->isInjectionEnabled = false;
	});
	engine->rpmCalculator.setRpmValue(1000);
	auto& hpfp = *engine->module<HpfpController>();
	auto& scheduler = *engine->module<TriggerScheduler>();
	EnginePhaseInfo phase{getTimeNowNt(), 100, 110, 100, 110};
	// Consume the initial configuration notification before arming the old chain.
	mainTriggerCallback(0, phase);
	hpfp.onFastCallback();
	ASSERT_TRUE(hpfp.m_running);
	scheduler.cancel(&hpfp.m_event);
	if (closing) {
		hpfp.m_event.scheduling.momentX = getTimeNowNt();
		HpfpController::pinTurnOn(&hpfp);
		ASSERT_TRUE(enginePins.hpfpValve.getLogicValue());
	} else {
		engine->scheduler.schedule(
				"old opening",
				&hpfp.m_event.scheduling,
				getTimeNowNt() + US2NT(1000),
				{HpfpController::pinTurnOn, &hpfp});
	}
	ASSERT_TRUE(bool(hpfp.m_event.scheduling.action));
	int obsoleteCount = 0;
	scheduler.schedule(&obsolete, EngPhase{105}, {+[](int* count) { ++*count; }, &obsoleteCount});
	const auto warningsBefore = eth.getWarningCounter();

	// Exercise configuration detection and the real main-trigger reset path.
	engineConfiguration->globalTriggerAngleOffset += 1;
	incrementGlobalConfigurationVersion();
	ASSERT_TRUE(engine->triggerCentral.isTriggerConfigChanged());
	// The host GPIO mock unconditionally reinitializes registered outputs on a
	// configuration update. Production preserves this unchanged pin: retain its
	// pre-update level so that an early mock shutdown cannot hide a lost close.
	enginePins.hpfpValve.setValue(closing);
	// Despite its name this counter counts every setValue, including LOW.
	const auto pinWritesBefore = enginePins.hpfpValve.unitTestTurnedOnCounter;
	mainTriggerCallback(0, phase);
	EXPECT_FALSE(engine->triggerCentral.isTriggerConfigChanged());
	EXPECT_EQ(0, scheduler.getQueueSizeForUnitTest());
	EXPECT_FALSE(hpfp.m_running);
	EXPECT_TRUE(bool(hpfp.m_event.scheduling.action));
	EXPECT_EQ(closing, enginePins.hpfpValve.getLogicValue());
	hpfp.onFastCallback();
	EXPECT_FALSE(hpfp.m_running);
	eth.moveTimeForwardAndInvokeEventsUs(10000);
	EXPECT_FALSE(enginePins.hpfpValve.getLogicValue());
	EXPECT_EQ(pinWritesBefore + (closing ? 1 : 0), enginePins.hpfpValve.unitTestTurnedOnCounter);
	EXPECT_EQ(0, obsoleteCount);
	EXPECT_EQ(0, scheduler.getQueueSizeForUnitTest());
	EXPECT_FALSE(bool(hpfp.m_event.scheduling.action));

	hpfp.onFastCallback();
	EXPECT_TRUE(hpfp.m_running);
	EXPECT_EQ(1, scheduler.getQueueSizeForUnitTest());
	hpfp.onFastCallback();
	EXPECT_EQ(1, scheduler.getQueueSizeForUnitTest());
	EXPECT_TRUE(scheduler.validateQueuesForUnitTest());
	EXPECT_EQ(warningsBefore, eth.getWarningCounter());
}

} // namespace

TEST(HPFP, TriggerConfigChangePreservesArmedCloseAndRestartsOnce) {
	verifyTriggerConfigChangeWithPendingHpfpTimer(true);
}

TEST(HPFP, TriggerConfigChangeSuppressesArmedOpenAndRestartsOnce) {
	verifyTriggerConfigChangeWithPendingHpfpTimer(false);
}

TEST(HPFP, IntegratedSchedule) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE, [](engine_configuration_s* cfg) {
		cfg->hpfpValvePin = Gpio::A2; // arbitrary
	});

	setCylinderCount(4);
	engineConfiguration->hpfpCamLobes = 3;
	engineConfiguration->hpfpPumpVolume = 0.2; // cc/lobe

	engineConfiguration->trigger.customTotalToothCount = 16;
	engineConfiguration->trigger.customSkippedToothCount = 0;
	eth.setTriggerType(trigger_type_e::TT_TOOTHED_WHEEL);
	setCamOperationMode();
	engineConfiguration->isFasterEngineSpinUpEnabled = true;

	eth.smartFireTriggerEvents2(/*count*/ 40, /*delay*/ 4);
	ASSERT_EQ(937, round(Sensor::getOrZero(SensorType::Rpm)));

	for (int i = 0; i < 100; i++) {
		eth.smartFireTriggerEvents2(/*count*/ 1, /*delay*/ 4);
		engine->periodicFastCallback();
	}
	/**
	 * overall this is a pretty lame test but helps to know that the whole on/off/on dance does in fact happen for HPFP
	 */
	// The trigger configuration change drops the old angle event and restarts the
	// HPFP chain. The two openings from the old schedule must not occur.
	ASSERT_EQ(29, enginePins.hpfpValve.unitTestTurnedOnCounter);
}
