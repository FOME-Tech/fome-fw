/*
 * @file injection_mode_transition.cpp
 *
 *  Created on: Jul 19, 2020
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"
#include "fuel_math.h"
#include "injector_model.h"

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
		// Injector 2 should be scheduled to open then close
		InjectorContext ctx;
		ctx.eventIndex = 1;
		// fire cyl 2+3 (batch)
		ctx.outputsMask = 1 << 1 | 1 << 2;

		void* ctxAsPtr = bit_cast<void*>(ctx);

		ASSERT_EQ(engine->scheduler.size(), 2);

		// Check that the action is correct - we don't care about the timing necessarily
		auto sched_open = engine->scheduler.getForUnitTest(0);
		ASSERT_EQ(sched_open->action.getArgument(), ctxAsPtr);
		ASSERT_EQ(sched_open->action.getCallback(), (void (*)(void*))startInjection);

		auto sched_close = engine->scheduler.getForUnitTest(1);
		// Next action should be closing the same injector
		ASSERT_EQ(sched_close->action.getArgument(), ctxAsPtr);
		ASSERT_EQ(sched_close->action.getCallback(), (void (*)(void*))endInjection);
	}

	// Run the engine for some revs
	for (size_t i = 0; i < 10; i++) {
		doRevolution(eth, 150);
	}

	// Check that no injectors are stuck open
	// Injectors 1/3 should be open
	EXPECT_EQ(enginePins.injectors[0].getOverlappingCounter(), 1);
	EXPECT_EQ(enginePins.injectors[1].getOverlappingCounter(), 1);
	EXPECT_EQ(enginePins.injectors[2].getOverlappingCounter(), 1);
	EXPECT_EQ(enginePins.injectors[3].getOverlappingCounter(), 0);

	// Trigger signal disappears but time progresses, all injectors should close
	eth.moveTimeForwardAndInvokeEventsUs(1e6);

	EXPECT_EQ(enginePins.injectors[0].getOverlappingCounter(), 0);
	EXPECT_EQ(enginePins.injectors[1].getOverlappingCounter(), 0);
	EXPECT_EQ(enginePins.injectors[2].getOverlappingCounter(), 0);
	EXPECT_EQ(enginePins.injectors[3].getOverlappingCounter(), 0);
}

namespace {
enum class StagedBatchTransition {
	DuringPulse,
	AfterPulse,
	KeepCranking,
};

// Port of rusEFI 857e7744 (issue #10414). FOME must close both batch outputs
// even if the event is rebuilt for sequential injection while its pulse is open.
void checkStagedBatchTransition(StagedBatchTransition transition) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	// EnginePins::reset omits secondary injectors. Isolate these tests even when
	// an assertion exits early, without hiding an unmatched open during the test.
	struct SecondaryPinCleanup {
		~SecondaryPinCleanup() {
			for (auto& pin : enginePins.injectorsStage2) {
				pin.reset();
			}
		}
	} cleanup;
	for (auto& pin : enginePins.injectorsStage2) {
		pin.reset();
	}

	engineConfiguration->firingOrder = FO_1_3_4_2;
	engineConfiguration->cranking.rpm = 400;
	engineConfiguration->crankingInjectionMode = IM_BATCH;
	engineConfiguration->injectionMode = IM_SEQUENTIAL;
	engineConfiguration->enableStagedInjection = true;
	engineConfiguration->injectorFlowAsMassFlow = true;
	engineConfiguration->injector.flow = 1;
	engineConfiguration->injectorSecondary.flow = 1;
	engineConfiguration->injectorCompensationMode = ICM_None;
	engineConfiguration->injectorNonlinearMode = INJ_None;
	engineConfiguration->minimumInjectionPulseWidth = 0;
	setArrayValues(engineConfiguration->injector.battLagCorr, 1.0f);
	setArrayValues(engineConfiguration->injectorSecondary.battLagCorr, 1.0f);
	setTable(config->injectorStagingTable, 50);
	eth.applyTriggerWaveform();
	engine->rpmCalculator.setRpmValue(390);
	ASSERT_TRUE(engine->rpmCalculator.isCranking());
	ASSERT_EQ(4, engine->engineState.cylinderCount);

	// First RPM invokes the fast callback, so set the input snapshot afterwards.
	// Batch mode halves the per-cycle mass, then staging splits it equally:
	// 0.04 g * 0.5 * 0.5 / (1 g/s) + 1 ms deadtime = 11 ms per stage.
	engine->engineState.injectionStage2Fraction = getStage2InjectionFraction(390, 100);
	ASSERT_FLOAT_EQ(0.5f, engine->engineState.injectionStage2Fraction);
	engine->engineState.injectionDuration = 11;
	engine->engineState.injectionOffset = 0;
	engine->engineState.shouldUpdateInjectionTiming = true;
	for (auto& cylinder : engine->cylinders) {
		cylinder.setInjectionMass(0.04f);
	}
	engine->module<InjectorModelPrimary>().unmock().prepare();
	engine->module<InjectorModelSecondary>().unmock().prepare();
	ASSERT_NEAR(11, engine->module<InjectorModelPrimary>()->getInjectionDuration(0.01f), 0.001f);
	ASSERT_NEAR(11, engine->module<InjectorModelSecondary>()->getInjectionDuration(0.01f), 0.001f);
	engine->injectionEvents.addFuelEvents();
	ASSERT_TRUE(engine->injectionEvents.isReady);
	auto& event = engine->injectionEvents.elements[0];
	// Firing order 1-3-4-2 pairs cylinders 1 and 4 in batch mode.
	ASSERT_EQ((1 << 0) | (1 << 3), event.calculateInjectorOutputMask());
	event.injectionStartAngle = 0;
	event.onTriggerTooth({getTimeNowNt(), 0, 10, 0, 10});
	ASSERT_EQ(1, eth.executeActions());
	ASSERT_EQ(2, engine->scheduler.size());
	for (size_t i : {0u, 3u}) {
		ASSERT_TRUE(enginePins.injectors[i].getLogicValue());
		ASSERT_TRUE(enginePins.injectorsStage2[i].getLogicValue());
		ASSERT_EQ(1, enginePins.injectorsStage2[i].getOverlappingCounter());
	}

	if (transition == StagedBatchTransition::DuringPulse) {
		eth.moveTimeForwardUs(1000);
		engine->rpmCalculator.setRpmValue(410);
		ASSERT_FALSE(engine->rpmCalculator.isCranking());
		// FOME does not rebuild automatically on this RPM transition. Force a
		// rebuild with the new mode while both original closes are still queued.
		engine->injectionEvents.addFuelEvents();
		ASSERT_TRUE(engine->injectionEvents.isReady);
		ASSERT_EQ(1 << 0, event.calculateInjectorOutputMask());
		ASSERT_EQ(2, engine->scheduler.size());
		ASSERT_TRUE(enginePins.injectorsStage2[3].getLogicValue());
	}

	// Run both original closing callbacks. Every output must be low with no
	// unmatched opens, including cylinder 4 which is no longer in event 0's mask.
	eth.moveTimeForwardUs(20000);
	ASSERT_EQ(2, eth.executeActions());
	ASSERT_EQ(nullptr, engine->scheduler.getHead());
	for (size_t i = 0; i < efi::size(enginePins.injectorsStage2); i++) {
		EXPECT_FALSE(enginePins.injectors[i].getLogicValue()) << i;
		EXPECT_EQ(0, enginePins.injectors[i].getOverlappingCounter()) << i;
		EXPECT_FALSE(enginePins.injectorsStage2[i].getLogicValue()) << i;
		EXPECT_EQ(0, enginePins.injectorsStage2[i].getOverlappingCounter()) << i;
	}

	if (transition == StagedBatchTransition::KeepCranking) {
		ASSERT_TRUE(engine->rpmCalculator.isCranking());
		return;
	}
	if (transition == StagedBatchTransition::AfterPulse) {
		engine->rpmCalculator.setRpmValue(410);
		ASSERT_FALSE(engine->rpmCalculator.isCranking());
		engine->injectionEvents.addFuelEvents();
		ASSERT_TRUE(engine->injectionEvents.isReady);
		ASSERT_EQ(1 << 0, event.calculateInjectorOutputMask());
	}

	// A subsequent sequential pulse for cylinder 4 must also return to zero,
	// rather than retaining an orphaned batch open underneath the new pulse.
	auto& nextEvent = engine->injectionEvents.elements[2];
	ASSERT_EQ(1 << 3, nextEvent.calculateInjectorOutputMask());
	nextEvent.injectionStartAngle = 0;
	nextEvent.onTriggerTooth({getTimeNowNt(), 0, 10, 0, 10});
	ASSERT_EQ(1, eth.executeActions());
	ASSERT_EQ(2, engine->scheduler.size());
	ASSERT_TRUE(enginePins.injectorsStage2[3].getLogicValue());
	ASSERT_EQ(1, enginePins.injectorsStage2[3].getOverlappingCounter());
	// Sequential mode uses the full cycle mass: 21 ms per stage.
	eth.moveTimeForwardUs(30000);
	ASSERT_EQ(2, eth.executeActions());
	EXPECT_EQ(nullptr, engine->scheduler.getHead());
	EXPECT_FALSE(enginePins.injectors[3].getLogicValue());
	EXPECT_EQ(0, enginePins.injectors[3].getOverlappingCounter());
	EXPECT_FALSE(enginePins.injectorsStage2[3].getLogicValue());
	EXPECT_EQ(0, enginePins.injectorsStage2[3].getOverlappingCounter());
}
} // namespace

TEST(fuelControl, CrankingToRunningClosesSecondaryBatchInjector) {
	checkStagedBatchTransition(StagedBatchTransition::DuringPulse);
}

TEST(fuelControl, StagedBatchPulseClosesWhileCranking) {
	checkStagedBatchTransition(StagedBatchTransition::KeepCranking);
}

TEST(fuelControl, CrankingToRunningAfterStagedBatchPulseClosesSecondaryInjector) {
	checkStagedBatchTransition(StagedBatchTransition::AfterPulse);
}
