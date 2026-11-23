/*
 * @file test_idle_controller.cpp
 *
 * @date Oct 17, 2013
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"

#include "efi_pid.h"
#include "idle_thread.h"
#include "electronic_throttle.h"

using ::testing::_;
using ::testing::StrictMock;

using ICP = IIdleController::Phase;
using TgtInfo = IIdleController::TargetInfo;

TEST(idle_v2, timingPid) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	IdleController dut;

	engineConfiguration->useIdleTimingPidControl = true;

	engineConfiguration->idleTimingPid.pFactor = 0.1;
	engineConfiguration->idleTimingPid.dFactor = 0.03;
	engineConfiguration->idleTimingPid.minValue = -10;
	engineConfiguration->idleTimingPid.maxValue = 10;
	dut.init();

	// Check that out of idle mode it doesn't do anything
	EXPECT_EQ(0, dut.getIdleTimingAdjustment(1050, 0, 1000, ICP::Cranking));
	EXPECT_EQ(0, dut.getIdleTimingAdjustment(1050, 0, 1000, ICP::Coasting));
	EXPECT_EQ(0, dut.getIdleTimingAdjustment(1050, 0, 1000, ICP::Running));

	// Check that it works in idle mode
	EXPECT_FLOAT_EQ(-5, dut.getIdleTimingAdjustment(1050, 0, 1000, ICP::Idling));

	// ...but not when disabled
	engineConfiguration->useIdleTimingPidControl = false;
	EXPECT_EQ(0, dut.getIdleTimingAdjustment(1050, 1000, 0, ICP::Idling));

	engineConfiguration->useIdleTimingPidControl = true;

	// Test P term
	EXPECT_FLOAT_EQ(5, dut.getIdleTimingAdjustment(950, 0, 1000, ICP::Idling));
	EXPECT_FLOAT_EQ(2.5, dut.getIdleTimingAdjustment(975, 0, 1000, ICP::Idling));
	EXPECT_FLOAT_EQ(0, dut.getIdleTimingAdjustment(1000, 0, 1000, ICP::Idling));
	EXPECT_FLOAT_EQ(-2.5, dut.getIdleTimingAdjustment(1025, 0, 1000, ICP::Idling));
	EXPECT_FLOAT_EQ(-5, dut.getIdleTimingAdjustment(1050, 0, 1000, ICP::Idling));

	// Test D term
	// Negative rate of change -> add timing
	// Positive rate of change -> remove timing
	EXPECT_FLOAT_EQ(3, dut.getIdleTimingAdjustment(1000, -100, 1000, ICP::Idling));
	EXPECT_FLOAT_EQ(0, dut.getIdleTimingAdjustment(1000, 0, 1000, ICP::Idling));
	EXPECT_FLOAT_EQ(-3, dut.getIdleTimingAdjustment(1000, 100, 1000, ICP::Idling));
}

TEST(idle_v2, testTargetRpm) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	IdleTargetController dut;

	for (size_t i = 0; i < efi::size(config->cltIdleRpmBins); i++) {
		config->cltIdleRpmBins[i] = i * 10;
		config->cltIdleRpm[i] = i * 100;
	}

	engineConfiguration->idlePidRpmUpperLimit = 50;
	EXPECT_EQ((TgtInfo{100, 150, 175}), dut.getTargetRpm(10));
	EXPECT_EQ((TgtInfo{500, 550, 575}), dut.getTargetRpm(50));

	engineConfiguration->idlePidRpmUpperLimit = 73;
	EXPECT_EQ((TgtInfo{100, 173, 209.5}), dut.getTargetRpm(10));
	EXPECT_EQ((TgtInfo{500, 573, 609.5}), dut.getTargetRpm(50));
}

TEST(idle_v2, testDeterminePhase) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	IdleTargetController dut;

	// TPS threshold 5% for easy test
	engineConfiguration->idlePidDeactivationTpsThreshold = 5;
	// Max VSS for idle is 10kph
	engineConfiguration->maxIdleVss = 10;
	// Disable inhibit-after-cranking for this test
	engineConfiguration->inhibitIdleAfterCrankingTime = 0;

	TgtInfo targetInfo;
	// Phase determination should ignore this!
	targetInfo.ClosedLoopTarget = 9999;

	// Idling threshold is 1000 + 100 rpm
	targetInfo.IdleEntryRpm = 1000 + 100;
	targetInfo.IdleExitRpm = 1000 + 100;

	// First test stopped engine
	engine->rpmCalculator.setRpmValue(0);
	EXPECT_EQ(ICP::Cranking, dut.determinePhase(0, targetInfo, false, 0, 10));

	// Now engine is running!
	// Controller doesn't need this other than for isCranking()
	engine->rpmCalculator.setRpmValue(1000);
	// Advance time so the inhibit-after-cranking timer (set to 0) is satisfied
	advanceTimeUs(1);

	// Test invalid TPS, but inside the idle window
	EXPECT_EQ(ICP::Running, dut.determinePhase(1000, targetInfo, true, 0, 10));

	// Valid TPS should now be inside the zone
	EXPECT_EQ(ICP::Idling, dut.determinePhase(1000, targetInfo, false, 0, 10));

	// Inside the zone, but vehicle speed too fast
	EXPECT_EQ(ICP::Running, dut.determinePhase(1000, targetInfo, false, 25, 10));

	// Check that shortly after cranking, the cranking taper inhibits coasting...
	EXPECT_EQ(ICP::CrankToIdleTaper, dut.determinePhase(1500, targetInfo, false, 0, 0.5f));
	// ...but allows closed loop
	EXPECT_EQ(ICP::Idling, dut.determinePhase(1050, targetInfo, false, 0, 0.5f));

	// Above TPS threshold should be outside the zone
	EXPECT_EQ(ICP::Running, dut.determinePhase(1000, targetInfo, true, 0, 10));

	// Above target, below (target + upperLimit) should be in idle zone
	EXPECT_EQ(ICP::Idling, dut.determinePhase(1099, targetInfo, false, 0, 10));

	// above upper limit and on throttle should be out of idle zone
	EXPECT_EQ(ICP::Running, dut.determinePhase(1101, targetInfo, true, 0, 10));

	// Below TPS but above RPM should be outside the zone
	EXPECT_EQ(ICP::Coasting, dut.determinePhase(1101, targetInfo, false, 0, 10));
	EXPECT_EQ(ICP::Coasting, dut.determinePhase(5000, targetInfo, false, 0, 10));

	// Check hysteresis behavior: entry RPM 1100, exit 1200
	targetInfo.IdleEntryRpm = 1000 + 100;
	targetInfo.IdleExitRpm = 1000 + 200;

	// Below entry: idling
	EXPECT_EQ(ICP::Idling, dut.determinePhase(1050, targetInfo, false, 0, 10));

	// Between thresholds: still idling
	EXPECT_EQ(ICP::Idling, dut.determinePhase(1150, targetInfo, false, 0, 10));

	// Above exit: coasting
	EXPECT_EQ(ICP::Coasting, dut.determinePhase(1250, targetInfo, false, 0, 10));

	// Between thresholds: still coasting
	EXPECT_EQ(ICP::Coasting, dut.determinePhase(1150, targetInfo, false, 0, 10));

	// Below entry: idling
	EXPECT_EQ(ICP::Idling, dut.determinePhase(1050, targetInfo, false, 0, 10));
}

TEST(idle_v2, inhibitIdleAfterCranking) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	IdleTargetController dut;

	engineConfiguration->idlePidDeactivationTpsThreshold = 5;
	engineConfiguration->maxIdleVss = 10;
	// Inhibit idle for 2 seconds after cranking
	engineConfiguration->inhibitIdleAfterCrankingTime = 2;

	TgtInfo targetInfo;
	targetInfo.ClosedLoopTarget = 9999;
	targetInfo.IdleEntryRpm = 1100;
	targetInfo.IdleExitRpm = 1100;

	// Start cranking - this resets the timer
	engine->rpmCalculator.setRpmValue(0);
	EXPECT_EQ(ICP::Cranking, dut.determinePhase(0, targetInfo, true, 0, 10));

	// Now engine is running
	engine->rpmCalculator.setRpmValue(1000);

	// Shortly after cranking, idle should be inhibited (coasting) even though RPM is in range
	EXPECT_EQ(ICP::Coasting, dut.determinePhase(1000, targetInfo, 0, 0, 10));

	// Advance time past the inhibit window
	advanceTimeUs(3'000'000);

	// Now idle should work normally
	EXPECT_EQ(ICP::Idling, dut.determinePhase(1000, targetInfo, 0, 0, 10));
}

TEST(idle_v2, inhibitIdleAfterCrankingDisabled) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	IdleTargetController dut;

	engineConfiguration->idlePidDeactivationTpsThreshold = 5;
	engineConfiguration->maxIdleVss = 10;
	// Disable the feature
	engineConfiguration->inhibitIdleAfterCrankingTime = 0;

	TgtInfo targetInfo;
	targetInfo.ClosedLoopTarget = 9999;
	targetInfo.IdleEntryRpm = 1100;
	targetInfo.IdleExitRpm = 1100;

	// Start cranking
	engine->rpmCalculator.setRpmValue(0);
	EXPECT_EQ(ICP::Cranking, dut.determinePhase(0, targetInfo, false, 0, 10));

	// Now engine is running - idle should work immediately since feature is disabled
	engine->rpmCalculator.setRpmValue(1000);
	advanceTimeUs(1);
	EXPECT_EQ(ICP::Idling, dut.determinePhase(1000, targetInfo, 0, 0, 10));
}

// Return-to-idle target ramp: idle target 940, entry threshold 940 + 500 = 1440, exit 1690
static void setupReturnRamp() {
	for (size_t i = 0; i < efi::size(config->cltIdleRpmBins); i++) {
		config->cltIdleRpmBins[i] = i * 10;
		config->cltIdleRpm[i] = 940;
	}

	engineConfiguration->idlePidRpmUpperLimit = 500;
	engineConfiguration->idleReturnTargetRamp = true;
	engineConfiguration->idleReturnRampDuration = 3;
	engineConfiguration->inhibitIdleAfterCrankingTime = 0;
	engineConfiguration->maxIdleVss = 0;

	// Finish the cranking taper so coasting reads as Coasting
	engineConfiguration->afterCrankingIACtaperDuration = 1;
	engine->rpmCalculator.onNewEngineCycle();
	engine->rpmCalculator.onNewEngineCycle();

	// Engine running
	engine->rpmCalculator.setRpmValue(1000);
	advanceTimeUs(1);
}

static IIdleTargetController::Output idleTargetStep(IdleTargetController& dut, float instantRpm, bool onThrottle) {
	engine->triggerCentral.instantRpm.m_instantRpm = instantRpm;
	return dut.getOutput(onThrottle);
}

TEST(idle_v2, returnRampFromCoasting) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	IdleTargetController dut;
	setupReturnRamp();

	// Coasting down, off throttle
	auto out = idleTargetStep(dut, 2000, false);
	EXPECT_EQ(ICP::Coasting, out.phase);
	EXPECT_FLOAT_EQ(940, out.target.ClosedLoopTarget);
	out = idleTargetStep(dut, 1500, false);
	EXPECT_EQ(ICP::Coasting, out.phase);

	// Drops below the entry threshold: the ramp starts where the engine is, from this very tick
	out = idleTargetStep(dut, 1430, false);
	EXPECT_EQ(ICP::Idling, out.phase);
	EXPECT_NEAR(1430, out.target.ClosedLoopTarget, 0.1);
	EXPECT_NEAR(490.0f / 500, out.returnRampFraction, 1e-4);

	// Halfway through the ramp
	advanceTimeUs(1'500'000);
	out = idleTargetStep(dut, 1100, false);
	EXPECT_EQ(ICP::Idling, out.phase);
	EXPECT_NEAR(940 + 490 / 2, out.target.ClosedLoopTarget, 0.1);
	EXPECT_NEAR(490.0f / 500 / 2, out.returnRampFraction, 1e-4);

	// Ramp complete, and stays there
	advanceTimeUs(1'500'000);
	out = idleTargetStep(dut, 950, false);
	EXPECT_NEAR(940, out.target.ClosedLoopTarget, 0.1);
	EXPECT_FLOAT_EQ(0, out.returnRampFraction);
	advanceTimeUs(10'000'000);
	EXPECT_NEAR(940, idleTargetStep(dut, 950, false).target.ClosedLoopTarget, 0.1);
}

TEST(idle_v2, returnRampLiftOffNearIdle) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	IdleTargetController dut;
	setupReturnRamp();

	// Creeping on a little throttle, then lift off at 968 RPM. The target must start at the engine,
	// not at the 1440 RPM entry threshold.
	EXPECT_EQ(ICP::Running, idleTargetStep(dut, 968, true).phase);

	auto out = idleTargetStep(dut, 968, false);
	EXPECT_EQ(ICP::Idling, out.phase);
	EXPECT_NEAR(968, out.target.ClosedLoopTarget, 0.1);
	// Only a small fraction of the full ramp, so carried-over air is mostly removed at once
	EXPECT_NEAR(28.0f / 500, out.returnRampFraction, 1e-4);

	advanceTimeUs(1'500'000);
	EXPECT_NEAR(954, idleTargetStep(dut, 960, false).target.ClosedLoopTarget, 0.1);

	advanceTimeUs(1'500'000);
	EXPECT_NEAR(940, idleTargetStep(dut, 945, false).target.ClosedLoopTarget, 0.1);
}

TEST(idle_v2, returnRampLiftOffBelowTarget) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	IdleTargetController dut;
	setupReturnRamp();

	// Lift off below the idle target: no ramp at all
	idleTargetStep(dut, 900, true);
	auto out = idleTargetStep(dut, 900, false);
	EXPECT_EQ(ICP::Idling, out.phase);
	EXPECT_NEAR(940, out.target.ClosedLoopTarget, 0.1);
	EXPECT_FLOAT_EQ(0, out.returnRampFraction);
}

TEST(idle_v2, returnRampStartClampedToEntryThreshold) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	IdleTargetController dut;
	setupReturnRamp();

	// Lift off between the entry (1440) and exit (1690) thresholds goes straight to Idling (the
	// coasting latch was cleared by Running). The ramp starts no higher than the entry threshold.
	idleTargetStep(dut, 1600, true);
	auto out = idleTargetStep(dut, 1600, false);
	EXPECT_EQ(ICP::Idling, out.phase);
	EXPECT_NEAR(1440, out.target.ClosedLoopTarget, 0.1);
}

TEST(idle_v2, returnRampDuration) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	IdleTargetController dut;
	setupReturnRamp();
	engineConfiguration->idleReturnRampDuration = 1;

	idleTargetStep(dut, 2000, false);
	EXPECT_NEAR(1439, idleTargetStep(dut, 1439, false).target.ClosedLoopTarget, 0.1);

	advanceTimeUs(500'000);
	EXPECT_NEAR(940 + 499 / 2.0, idleTargetStep(dut, 1200, false).target.ClosedLoopTarget, 0.1);

	advanceTimeUs(500'000);
	EXPECT_NEAR(940, idleTargetStep(dut, 1000, false).target.ClosedLoopTarget, 0.1);
}

TEST(idle_v2, returnRampRestartsOnEachEntry) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	IdleTargetController dut;
	setupReturnRamp();

	// First return from coasting, part way down the ramp...
	idleTargetStep(dut, 2000, false);
	idleTargetStep(dut, 1439, false);
	advanceTimeUs(1'000'000);
	EXPECT_EQ(ICP::Idling, idleTargetStep(dut, 1200, false).phase);

	// ...then a quick blip, lifting off at 1100: the new ramp starts at 1100, not where the old one was
	EXPECT_EQ(ICP::Running, idleTargetStep(dut, 1100, true).phase);
	auto out = idleTargetStep(dut, 1100, false);
	EXPECT_EQ(ICP::Idling, out.phase);
	EXPECT_NEAR(1100, out.target.ClosedLoopTarget, 0.1);
}

TEST(idle_v2, returnRampDisabled) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	IdleTargetController dut;
	setupReturnRamp();
	engineConfiguration->idleReturnTargetRamp = false;

	idleTargetStep(dut, 2000, false);
	auto out = idleTargetStep(dut, 1430, false);
	EXPECT_EQ(ICP::Idling, out.phase);
	EXPECT_NEAR(940, out.target.ClosedLoopTarget, 0.1);
	EXPECT_FLOAT_EQ(0, out.returnRampFraction);

	// A zero duration also means no ramp
	engineConfiguration->idleReturnTargetRamp = true;
	engineConfiguration->idleReturnRampDuration = 0;
	idleTargetStep(dut, 1100, true);
	out = idleTargetStep(dut, 1100, false);
	EXPECT_EQ(ICP::Idling, out.phase);
	EXPECT_NEAR(940, out.target.ClosedLoopTarget, 0.1);
}

TEST(idle_v2, crankingOpenLoop) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	IdleController dut;

	engineConfiguration->crankingIACposition = 50;

	for (size_t i = 0; i < efi::size(config->cltCrankingCorrBins); i++) {
		config->cltCrankingCorrBins[i] = i * 10;
		config->cltCrankingCorr[i] = i * 0.1f;

		// different values in running so we can tell which one is used
		config->cltIdleCorrBins[i] = i * 10;
		config->cltIdleCorr[i] = i * 0.2f;
	}

	// First test without override (ie, normal running CLT corr table)
	EXPECT_FLOAT_EQ(10, dut.getCrankingOpenLoop(10));
	EXPECT_FLOAT_EQ(50, dut.getCrankingOpenLoop(50));

	// Test with override (use separate table)
	engineConfiguration->overrideCrankingIacSetting = true;
	EXPECT_FLOAT_EQ(5, dut.getCrankingOpenLoop(10));
	EXPECT_FLOAT_EQ(25, dut.getCrankingOpenLoop(50));
}

TEST(idle_v2, runningOpenLoopBasic) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	IdleController dut;

	engineConfiguration->manIdlePosition = 50;

	for (size_t i = 0; i < efi::size(config->cltIdleCorrBins); i++) {
		config->cltIdleCorrBins[i] = i * 10;
		config->cltIdleCorr[i] = i * 0.1f;
	}

	EXPECT_FLOAT_EQ(5, dut.getRunningOpenLoop(10));
	EXPECT_FLOAT_EQ(25, dut.getRunningOpenLoop(50));
}

TEST(idle_v2, runningFanAcBump) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	IdleController dut;

	engineConfiguration->manIdlePosition = 50;
	engineConfiguration->acIdleExtraOffset = 9;
	engineConfiguration->fan1ExtraIdle = 7;
	engineConfiguration->fan2ExtraIdle = 3;

	setArrayValues(config->cltIdleCorr, 1.0f);

	// Start with fan off
	enginePins.fanRelay.setValue(0);

	// Should be base position
	EXPECT_FLOAT_EQ(50, dut.getRunningOpenLoop(10));

	// Turn on A/C!
	engine->module<AcController>()->acButtonState = true;
	EXPECT_FLOAT_EQ(50 + 9, dut.getRunningOpenLoop(10));
	engine->module<AcController>()->acButtonState = false;

	// Begin A/C Pressure Switch testing
	// engineConfiguration->acSwitch = Gpio::G1;
	engineConfiguration->acPressureSwitch = Gpio::G2;
	// setMockState(engineConfiguration->acSwitch, false);
	// setMockState(engineConfiguration->acPressureSwitch, false);
	engine->module<AcController>()->acPressureSwitchState = false;
	EXPECT_FLOAT_EQ(50, dut.getRunningOpenLoop(10));

	// setMockState(engineConfiguration->acSwitch, true);
	engine->module<AcController>()->acButtonState = true;
	EXPECT_FLOAT_EQ(50, dut.getRunningOpenLoop(10));

	// setMockState(engineConfiguration->acPressureSwitch, true);
	engine->module<AcController>()->acPressureSwitchState = true;
	EXPECT_FLOAT_EQ(50 + 9, dut.getRunningOpenLoop(10));

	// setMockState(engineConfiguration->acSwitch, false);
	engine->module<AcController>()->acButtonState = false;
	EXPECT_FLOAT_EQ(50, dut.getRunningOpenLoop(10));

	// engineConfiguration->acSwitch = Gpio::Unassigned;
	// setMockState(engineConfiguration->acPressureSwitch, false);
	engineConfiguration->acPressureSwitch = Gpio::Unassigned;
	engine->module<AcController>()->acPressureSwitchState = false;
	// End A/C Pressure Switch testing

	// Turn the fan on!
	enginePins.fanRelay.setValue(1);
	EXPECT_FLOAT_EQ(50 + 7, dut.getRunningOpenLoop(10));
	enginePins.fanRelay.setValue(0);

	// Turn on the other fan!
	enginePins.fanRelay2.setValue(1);
	EXPECT_FLOAT_EQ(50 + 3, dut.getRunningOpenLoop(10));

	// Turn on everything!
	engine->module<AcController>()->acButtonState = true;
	enginePins.fanRelay.setValue(1);
	enginePins.fanRelay2.setValue(1);
	EXPECT_FLOAT_EQ(50 + 9 + 7 + 3, dut.getRunningOpenLoop(10));
}

struct MockOpenLoopIdler : public IdleController {
	MOCK_METHOD(float, getCrankingOpenLoop, (float clt), (const, override));
	MOCK_METHOD(float, getRunningOpenLoop, (float clt), (override));
};

TEST(idle_v2, testOpenLoopCranking) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	StrictMock<MockOpenLoopIdler> dut;

	engineConfiguration->overrideCrankingIacSetting = true;

	EXPECT_CALL(dut, getCrankingOpenLoop(30)).WillOnce(Return(44));

	// Should return the value from getCrankingOpenLoop, and ignore running numbers
	EXPECT_FLOAT_EQ(44, dut.getOpenLoop(ICP::Cranking, 0, 30, 0));
}

TEST(idle_v2, openLoopRunningTaper) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	StrictMock<MockOpenLoopIdler> dut;

	EXPECT_CALL(dut, getRunningOpenLoop(30)).WillRepeatedly(Return(25));
	EXPECT_CALL(dut, getCrankingOpenLoop(30)).WillRepeatedly(Return(75));

	// 0 cycles - no taper yet, pure cranking value
	EXPECT_FLOAT_EQ(75, dut.getOpenLoop(ICP::Running, 0, 30, 0));
	EXPECT_FLOAT_EQ(75, dut.getOpenLoop(ICP::CrankToIdleTaper, 0, 30, 0));

	// 1/2 taper - half way, 50% each value -> outputs 50
	EXPECT_FLOAT_EQ(50, dut.getOpenLoop(ICP::Running, 0, 30, 0.5f));
	EXPECT_FLOAT_EQ(50, dut.getOpenLoop(ICP::CrankToIdleTaper, 0, 30, 0.5f));

	// 1x taper - fully tapered, should be running value
	EXPECT_FLOAT_EQ(25, dut.getOpenLoop(ICP::Running, 0, 30, 1.0f));
	EXPECT_FLOAT_EQ(25, dut.getOpenLoop(ICP::CrankToIdleTaper, 0, 30, 1.0f));

	// 2x taper - still fully tapered, should be running value
	EXPECT_FLOAT_EQ(25, dut.getOpenLoop(ICP::Running, 0, 30, 2.0f));
	EXPECT_FLOAT_EQ(25, dut.getOpenLoop(ICP::CrankToIdleTaper, 0, 30, 2.0f));
}

TEST(idle_v2, getCrankingTaperFraction) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	IdleTargetController dut;

	float expectedClt = 37;
	engineConfiguration->afterCrankingIACtaperDuration = 500;
	engineConfiguration->useCrankingIdleTaperTableSetting = false;

	// 0 cycles - no taper yet, pure cranking value
	EXPECT_FLOAT_EQ(0, dut.getCrankingTaperFraction(expectedClt));

	// 250 cycles - half way, 50% each value -> outputs 50
	for (size_t i = 0; i < 250; i++) {
		engine->rpmCalculator.onNewEngineCycle();
	}
	EXPECT_FLOAT_EQ(0.5f, dut.getCrankingTaperFraction(expectedClt));

	// 500 cycles - fully tapered, should be running value
	for (size_t i = 0; i < 250; i++) {
		engine->rpmCalculator.onNewEngineCycle();
	}
	EXPECT_FLOAT_EQ(1, dut.getCrankingTaperFraction(expectedClt));

	// 1000 cycles - still fully tapered, should be running value
	for (size_t i = 0; i < 500; i++) {
		engine->rpmCalculator.onNewEngineCycle();
	}
	EXPECT_FLOAT_EQ(2, dut.getCrankingTaperFraction(expectedClt));
}

TEST(idle_v2, getCrankingTaperFractionWithMultiplier) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	IdleTargetController dut;

	float expectedClt = 40;

	engineConfiguration->afterCrankingIACtaperDuration = 200;
	engineConfiguration->useCrankingIdleTaperTableSetting = true;

	float curve[CLT_CRANKING_CURVE_SIZE] = {
			1.0, // 0C
			1.0, // 10C
			1.0, // 20C
			0.5, // 30C
			0.4, // 40C
			0.3, // 50C
			0.2, // 60C
			0.1	 // 70C
	};

	for (int i = 0; i < CLT_CRANKING_CURVE_SIZE; i++) {
		config->cltCrankingTaperCorrBins[i] = i * 10;
		config->cltCrankingTaperCorr[i] = curve[i];
	}

	// 0 cycles - no taper yet, pure cranking value
	EXPECT_FLOAT_EQ(0, dut.getCrankingTaperFraction(expectedClt));

	// 50 cycles in - total taper duration should be 200 * 0.4 (40C) = 80 cyclyes instead of 200
	for (size_t i = 0; i < 50; i++) {
		engine->rpmCalculator.onNewEngineCycle();
	}
	// So 80*0.625 = 50 cycles
	EXPECT_FLOAT_EQ(0.625f, dut.getCrankingTaperFraction(expectedClt));
	// testing 20C colder, should use 1.0f so 200*0.25 = 50 cycles
	EXPECT_FLOAT_EQ(0.25f, dut.getCrankingTaperFraction(expectedClt - 20));

	// 200 cycles in - total taper duration should be done by 40C
	for (size_t i = 0; i < 150; i++) {
		engine->rpmCalculator.onNewEngineCycle();
	}
	// Taper last only 80 cycles, so 2.5*80 = 200 cycles
	EXPECT_FLOAT_EQ(2.5f, dut.getCrankingTaperFraction(expectedClt));
	// Taper last full length
	EXPECT_FLOAT_EQ(1.0f, dut.getCrankingTaperFraction(expectedClt - 20));
}

TEST(idle_v2, openLoopCoastingTable) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	IdleController dut;

	// enable & configure feature
	engineConfiguration->useIacTableForCoasting = true;
	for (size_t i = 0; i < CLT_CURVE_SIZE; i++) {
		config->iacCoastingRpmBins[i] = 100 * i;
		config->iacCoasting[i] = 5 * i;
	}

	EXPECT_FLOAT_EQ(40, dut.getOpenLoop(ICP::Coasting, 800, 0, 2));
	EXPECT_FLOAT_EQ(75, dut.getOpenLoop(ICP::Coasting, 1500, 0, 2));
}

TEST(idle_v2, openLoopDashpotAir) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	IdleController dut;

	engineConfiguration->manIdlePosition = 30;
	setArrayValues(config->cltIdleCorr, 1.0f);
	engineConfiguration->idleDashpotAir = 10;
	engineConfiguration->useIacTableForCoasting = false;

	// Added while off idle...
	EXPECT_FLOAT_EQ(40, dut.getOpenLoop(ICP::Running, 1500, 0, 2));
	EXPECT_FLOAT_EQ(40, dut.getOpenLoop(ICP::Coasting, 1500, 0, 2));

	// ...but not while idling, or in the cranking taper
	EXPECT_FLOAT_EQ(30, dut.getOpenLoop(ICP::Idling, 900, 0, 2));
	EXPECT_FLOAT_EQ(30, dut.getOpenLoop(ICP::CrankToIdleTaper, 1500, 0, 2));

	// The coasting table sets the whole position while coasting: no dashpot air on top
	engineConfiguration->useIacTableForCoasting = true;
	setArrayValues(config->iacCoasting, 45);
	EXPECT_FLOAT_EQ(45, dut.getOpenLoop(ICP::Coasting, 1500, 0, 2));
	EXPECT_FLOAT_EQ(40, dut.getOpenLoop(ICP::Running, 1500, 0, 2));
}

TEST(idle_v2, closedLoopBasic) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	IdleController dut;
	dut.init();

	// Not testing PID here, so we can set very simple PID gains
	engineConfiguration->idleRpmPid.pFactor = 0.5; // 0.5 output per 1 RPM error = 50% per 100 rpm
	engineConfiguration->idleRpmPid.iFactor = 0;
	engineConfiguration->idleRpmPid.dFactor = 0;
	engineConfiguration->idleRpmPid.iFactor = 0;
	engineConfiguration->idleRpmPid.minValue = -50;
	engineConfiguration->idleRpmPid.maxValue = 50;

	// burn one update then advance time 5 seconds to avoid difficulty from wasResetPid
	dut.getClosedLoop(ICP::Idling, 900, 0, 900);
	advanceTimeUs(5'000'000);

	// Test above target, should return negative
	EXPECT_FLOAT_EQ(-25, dut.getClosedLoop(ICP::Idling, /*rpm*/ 950, 0, /*tgt*/ 900));

	// Below target, should return positive
	EXPECT_FLOAT_EQ(25, dut.getClosedLoop(ICP::Idling, /*rpm*/ 850, 0, /*tgt*/ 900));
}

TEST(idle_v2, closedLoopInjectedRpmRate) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	IdleController dut;
	dut.init();

	// PID should be using rpmRate for the d term, rather than calculating for itself
	engineConfiguration->idleRpmPid.pFactor = 0;
	engineConfiguration->idleRpmPid.iFactor = 0;
	engineConfiguration->idleRpmPid.dFactor = 0.1;
	engineConfiguration->idleRpmPid.iFactor = 0;
	engineConfiguration->idleRpmPid.minValue = -50;
	engineConfiguration->idleRpmPid.maxValue = 50;

	// burn one update then advance time 5 seconds to avoid difficulty from wasResetPid
	dut.getClosedLoop(ICP::Idling, 900, 0, 900);
	advanceTimeUs(5'000'000);

	// Positive rpmRate -> negative output
	EXPECT_FLOAT_EQ(-10, dut.getClosedLoop(ICP::Idling, /*rpm*/ 900, /*rpmRate*/ 100, /*tgt*/ 900));

	// Negative rpmRate -> positive output
	EXPECT_FLOAT_EQ(10, dut.getClosedLoop(ICP::Idling, /*rpm*/ 900, /*rpmRate*/ -100, /*tgt*/ 900));
}

TEST(idle_v2, RunningToIdleTransition) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	IdleController dut;
	dut.init();

	engineConfiguration->idleRpmPid.pFactor = 0.5;
	engineConfiguration->idleRpmPid.iFactor = 0.0040;
	engineConfiguration->idleRpmPid.dFactor = 0.0001;
	engineConfiguration->idleRpmPid.minValue = -50;
	engineConfiguration->idleRpmPid.maxValue = 50;

	engineConfiguration->alwaysResetPidLeavingIdle = true;

	SensorResult expectedTps = 0;
	float expectedClt = 37;
	Sensor::setMockValue(SensorType::DriverThrottleIntent, expectedTps.Value);
	Sensor::setMockValue(SensorType::Clt, expectedClt);
	Sensor::setMockValue(SensorType::VehicleSpeed, 15.0);

	// we are on running state still, so 0 idle position
	EXPECT_EQ(0, dut.getClosedLoop(ICP::Running, 950, 0, 1100));
	dut.getIdlePid()->postState(engine->outputChannels.idleStatus);

	EXPECT_EQ(0, engine->outputChannels.idleStatus.dTerm);
	EXPECT_EQ(0, engine->outputChannels.idleStatus.iTerm);
	EXPECT_EQ(0, engine->outputChannels.idleStatus.pTerm);
	advanceTimeUs(5'000'000);

	// now we are idling
	EXPECT_NEAR(50, dut.getClosedLoop(ICP::Idling, 950, 0, 1100), EPS2D);
	EXPECT_NEAR(0, dut.getIdlePid()->getIntegration(), EPS2D);

	// still idle, add some error:
	EXPECT_NEAR(50, dut.getClosedLoop(ICP::Idling, 950, 0, 1120), EPS2D);
	EXPECT_NEAR(0.01, dut.getIdlePid()->getIntegration(), EPS2D);

	// back to running mode, should reset all:
	EXPECT_EQ(0, dut.getClosedLoop(ICP::Running, 950, 0, 1100));
	EXPECT_NEAR(0, dut.getIdlePid()->getIntegration(), EPS2D);
}

struct IntegrationIdleMock : public IdleController {
	MOCK_METHOD(float, getOpenLoop, (ICP phase, float rpm, float clt, float crankingTaperFraction), (override));
	MOCK_METHOD(float, getClosedLoop, (ICP phase, float rpm, float rpmRate, float target), (override));
};

// Installs a mock IdleTargetController in the module list whose getOutput() returns the given
// target/phase/taper. This is where getIdlePosition() sources phase determination from.
static void mockIdleTarget(
		MockIdleTargetController& mock,
		TgtInfo target,
		ICP phase,
		float crankingTaperFraction,
		float returnRampFraction = 0) {
	IIdleTargetController::Output out;
	out.target = target;
	out.phase = phase;
	out.crankingTaperFraction = crankingTaperFraction;
	out.returnRampFraction = returnRampFraction;
	EXPECT_CALL(mock, getOutput(_)).WillRepeatedly(Return(out));
	engine->engineModules.get<IdleTargetController>().set(&mock);
}

TEST(idle_v2, IntegrationManual) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	StrictMock<IntegrationIdleMock> dut;

	SensorResult expectedTps = 1;
	float expectedClt = 37;
	Sensor::setMockValue(SensorType::DriverThrottleIntent, expectedTps.Value);
	Sensor::setMockValue(SensorType::Clt, expectedClt);
	Sensor::setMockValue(SensorType::VehicleSpeed, 15.0);

	TgtInfo target{1000, 1100, 1100};

	// Target of 1000 rpm, idling, 30% through the cranking taper
	MockIdleTargetController mockTarget;
	mockIdleTarget(mockTarget, target, ICP::Idling, 0.3f);

	// Open loop should be asked for an open loop position
	EXPECT_CALL(dut, getOpenLoop(ICP::Idling, 950, expectedClt, 0.3f)).WillOnce(Return(13));

	// getClosedLoop() should not be called!

	EXPECT_EQ(13, dut.getIdlePosition(950, 0));
}

TEST(idle_v2, IntegrationAutomatic) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	StrictMock<IntegrationIdleMock> dut;

	engineConfiguration->idleMode = IM_AUTO;

	SensorResult expectedTps = 1;
	float expectedClt = 37;
	Sensor::setMockValue(SensorType::DriverThrottleIntent, expectedTps.Value);
	Sensor::setMockValue(SensorType::Clt, expectedClt);
	Sensor::setMockValue(SensorType::VehicleSpeed, 15.0);

	TgtInfo target{1000, 1100, 1100};

	// Target of 1000 rpm, idling, 40% through the cranking taper
	MockIdleTargetController mockTarget;
	mockIdleTarget(mockTarget, target, ICP::Idling, 0.4f);

	// Open loop should be asked for an open loop position
	EXPECT_CALL(dut, getOpenLoop(ICP::Idling, 950, expectedClt, 0.4f)).WillOnce(Return(13));

	// Closed loop should get called
	EXPECT_CALL(dut, getClosedLoop(ICP::Idling, 950, 100, 1000)).WillOnce(Return(7));

	// Result should be open + closed
	EXPECT_EQ(13 + 7, dut.getIdlePosition(950, 100));
}

TEST(idle_v2, IntegrationClamping) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	StrictMock<IntegrationIdleMock> dut;

	engineConfiguration->idleMode = IM_AUTO;

	SensorResult expectedTps = 1;
	float expectedClt = 37;
	Sensor::setMockValue(SensorType::DriverThrottleIntent, expectedTps.Value);
	Sensor::setMockValue(SensorType::Clt, expectedClt);
	Sensor::setMockValue(SensorType::VehicleSpeed, 15.0);

	TgtInfo target{1000, 1100, 1100};

	// Target of 1000 rpm, idling, 50% through the cranking taper
	MockIdleTargetController mockTarget;
	mockIdleTarget(mockTarget, target, ICP::Idling, 0.5f);

	// Open loop should be asked for an open loop position
	EXPECT_CALL(dut, getOpenLoop(ICP::Idling, 950, expectedClt, 0.5f)).WillOnce(Return(75));

	// Closed loop should get called
	EXPECT_CALL(dut, getClosedLoop(ICP::Idling, 950, 100, 1000)).WillOnce(Return(75));

	// Result would be 75 + 75 = 150, but it should clamp to 100
	EXPECT_EQ(100, dut.getIdlePosition(950, 100));
}

// Open loop idle at 30%, open loop mode so the position is just the open loop
static void setupReturnToIdleAir() {
	engineConfiguration->idleMode = IM_MANUAL;
	engineConfiguration->manIdlePosition = 30;
	setArrayValues(config->cltIdleCorr, 1.0f);
	engineConfiguration->useIacTableForCoasting = false;
	engineConfiguration->idleDashpotAir = 10;

	Sensor::setMockValue(SensorType::DriverThrottleIntent, 0);
	Sensor::setMockValue(SensorType::Clt, 37);
}

static float returnToIdleStep(
		IdleController& dut,
		MockIdleTargetController& mock,
		ICP phase,
		float returnRampFraction,
		float crankingTaper = 2) {
	mockIdleTarget(mock, TgtInfo{940, 1440, 1690}, phase, crankingTaper, returnRampFraction);
	return dut.getIdlePosition(1000, 0);
}

TEST(idle_v2, returnToIdleAirDashpot) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	IdleController dut;
	dut.init();
	setupReturnToIdleAir();
	MockIdleTargetController mock;

	// Dashpot air while on throttle
	EXPECT_FLOAT_EQ(40, returnToIdleStep(dut, mock, ICP::Running, 0));

	// Returning to idle at the top of the ramp: no step down, then it ramps out with the ramp
	EXPECT_FLOAT_EQ(40, returnToIdleStep(dut, mock, ICP::Idling, 1));
	EXPECT_FLOAT_EQ(40, dut.openLoop);
	EXPECT_FLOAT_EQ(35, returnToIdleStep(dut, mock, ICP::Idling, 0.5f));
	EXPECT_FLOAT_EQ(30, returnToIdleStep(dut, mock, ICP::Idling, 0));
	EXPECT_FLOAT_EQ(30, returnToIdleStep(dut, mock, ICP::Idling, 0));

	// Throttle again, then lift off near idle speed: the ramp is mostly gone, and so is the air
	EXPECT_FLOAT_EQ(40, returnToIdleStep(dut, mock, ICP::Running, 0));
	EXPECT_FLOAT_EQ(31, returnToIdleStep(dut, mock, ICP::Idling, 0.1f));

	// Ramp disabled (fraction 0): the dashpot air is removed at once
	EXPECT_FLOAT_EQ(40, returnToIdleStep(dut, mock, ICP::Running, 0));
	EXPECT_FLOAT_EQ(30, returnToIdleStep(dut, mock, ICP::Idling, 0));
}

TEST(idle_v2, returnToIdleAirCoastingTable) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	IdleController dut;
	dut.init();
	setupReturnToIdleAir();
	MockIdleTargetController mock;

	engineConfiguration->useIacTableForCoasting = true;
	setArrayValues(config->iacCoasting, 45);

	// Coasting table position, without dashpot air on top
	EXPECT_FLOAT_EQ(45, returnToIdleStep(dut, mock, ICP::Coasting, 0));

	// Ramps out from the coasting table position, not from idle + dashpot air
	EXPECT_FLOAT_EQ(45, returnToIdleStep(dut, mock, ICP::Idling, 1));
	EXPECT_FLOAT_EQ(37.5f, returnToIdleStep(dut, mock, ICP::Idling, 0.5f));
	EXPECT_FLOAT_EQ(30, returnToIdleStep(dut, mock, ICP::Idling, 0));

	// A coasting table below the idle position is not carried over (never takes air away)
	setArrayValues(config->iacCoasting, 20);
	EXPECT_FLOAT_EQ(20, returnToIdleStep(dut, mock, ICP::Coasting, 0));
	EXPECT_FLOAT_EQ(30, returnToIdleStep(dut, mock, ICP::Idling, 1));
}

TEST(idle_v2, returnToIdleAirNotAfterCranking) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	IdleController dut;
	dut.init();
	setupReturnToIdleAir();
	MockIdleTargetController mock;

	engineConfiguration->overrideCrankingIacSetting = false;
	engineConfiguration->crankingIACposition = 70;

	// Half way through the cranking taper: 70 -> 30
	EXPECT_FLOAT_EQ(50, returnToIdleStep(dut, mock, ICP::CrankToIdleTaper, 1, 0.5f));

	// Entering idle from the cranking taper carries nothing over: the cranking taper continues on its own
	EXPECT_FLOAT_EQ(46, returnToIdleStep(dut, mock, ICP::Idling, 1, 0.6f));
	EXPECT_FLOAT_EQ(0, dut.returnToIdleAir);
}

// Whole chain: real target controller deciding phase and ramp, coasting table, return to idle
TEST(idle_v2, returnToIdleCoastDown) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	IdleController dut;
	dut.init();
	setupReturnRamp();
	setupReturnToIdleAir();

	engineConfiguration->useIacTableForCoasting = true;
	setArrayValues(config->iacCoasting, 45);

	auto step = [&](float rpm) {
		engine->triggerCentral.instantRpm.m_instantRpm = rpm;
		return dut.getIdlePosition(rpm, 0);
	};

	EXPECT_FLOAT_EQ(45, step(2000));
	EXPECT_FLOAT_EQ(45, step(1500));

	// Enters idle at 1439: 499/500 of the full ramp, so almost all of the extra 15% is carried over
	EXPECT_NEAR(30 + 15 * 499.0f / 500, step(1439), 1e-3);
	EXPECT_NEAR(940 + 499, engine->module<IdleTargetController>().unmock().idleTarget, 1);

	advanceTimeUs(1'500'000);
	EXPECT_NEAR(30 + 15 * 499.0f / 500 / 2, step(1150), 1e-3);

	advanceTimeUs(1'500'000);
	EXPECT_FLOAT_EQ(30, step(960));
	EXPECT_EQ(940, engine->module<IdleTargetController>().unmock().idleTarget);
}
