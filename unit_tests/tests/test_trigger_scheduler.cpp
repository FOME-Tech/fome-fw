/*
 * Lifecycle of events shared by the trigger-tooth queue and the time scheduler.
 */

#include "pch.h"
#include "spark_logic.h"

namespace {

void countAction(int* count) {
	++*count;
}

EnginePhaseInfo phaseAtCurrentTooth() {
	EnginePhaseInfo phase;
	phase.timestamp = getTimeNowNt();
	phase.currentEngPhase = EngPhase{100};
	phase.nextEngPhase = EngPhase{110};
	phase.currentTrgPhase = TrgPhase{100};
	phase.nextTrgPhase = TrgPhase{110};
	return phase;
}

// Schedule() can run a timer callback inline. Inject a cancellation or stop while
// the first of two due events is being promoted to the time scheduler.
class InterleavingExecutor : public Scheduler {
public:
	TriggerScheduler* triggerScheduler = nullptr;
	AngleBasedEvent* victim = nullptr;
	bool stopOnSchedule = false;
	std::vector<scheduling_s*> scheduled;

	void schedule(const char*, scheduling_s* scheduling, efitick_t, action_s) override {
		scheduled.push_back(scheduling);
		if (scheduled.size() == 1) {
			if (stopOnSchedule) {
				triggerScheduler->flush();
			} else {
				triggerScheduler->cancel(victim);
			}
		}
	}

	void cancel(scheduling_s*) override {}
};

} // namespace

TEST(TriggerScheduler, engineStopEmptiesQueueButKeepsArmedTimer) {
	AngleBasedEvent event;
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto* triggerScheduler = &*engine->module<TriggerScheduler>();
	int fallbackCount = 0;

	triggerScheduler->schedule(&event, EngPhase{123}, {countAction, &fallbackCount});
	engine->scheduler.schedule(
			"overdwell", &event.scheduling, getTimeNowNt() + US2NT(2000), {countAction, &fallbackCount});
	ASSERT_EQ(1, triggerScheduler->getQueueSizeForUnitTest());

	engine->OnTriggerSynchronizationLost();
	EXPECT_EQ(0, triggerScheduler->getQueueSizeForUnitTest());
	ASSERT_EQ(1, engine->scheduler.size());
	eth.moveTimeForwardAndInvokeEventsUs(2000);
	EXPECT_EQ(1, fallbackCount);

	// A new cycle can reuse the same event without a duplicate-list warning.
	triggerScheduler->schedule(&event, EngPhase{123}, {countAction, &fallbackCount});
	EXPECT_EQ(1, triggerScheduler->getQueueSizeForUnitTest());
}

TEST(TriggerScheduler, scheduleByTimeReplacesStaleEventAndOverdwellTimer) {
	AngleBasedEvent event;
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto* triggerScheduler = &*engine->module<TriggerScheduler>();
	engine->rpmCalculator.oneDegreeUs = 100;
	int staleCount = 0;
	int fallbackCount = 0;
	int newCount = 0;

	triggerScheduler->schedule(&event, EngPhase{123}, {countAction, &staleCount});
	engine->scheduler.schedule(
			"overdwell", &event.scheduling, getTimeNowNt() + US2NT(2000), {countAction, &fallbackCount});
	ASSERT_EQ(1, triggerScheduler->getQueueSizeForUnitTest());

	EXPECT_TRUE(
			triggerScheduler->scheduleOrQueue(&event, EngPhase{105}, {countAction, &newCount}, phaseAtCurrentTooth()));
	EXPECT_EQ(0, triggerScheduler->getQueueSizeForUnitTest());
	ASSERT_EQ(1, engine->scheduler.size());

	eth.moveTimeForwardAndInvokeEventsUs(3000);
	EXPECT_EQ(1, newCount);
	EXPECT_EQ(0, staleCount);
	EXPECT_EQ(0, fallbackCount);
	EXPECT_EQ(0, engine->scheduler.size());
}

TEST(TriggerScheduler, toothPromotionReplacesOverdwellTimer) {
	AngleBasedEvent event;
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto* triggerScheduler = &*engine->module<TriggerScheduler>();
	engine->rpmCalculator.oneDegreeUs = 100;
	int sparkCount = 0;
	int fallbackCount = 0;

	triggerScheduler->schedule(&event, EngPhase{105}, {countAction, &sparkCount});
	engine->scheduler.schedule(
			"overdwell", &event.scheduling, getTimeNowNt() + US2NT(2000), {countAction, &fallbackCount});
	triggerScheduler->onEnginePhase(1000, phaseAtCurrentTooth());
	EXPECT_EQ(0, triggerScheduler->getQueueSizeForUnitTest());
	ASSERT_EQ(1, engine->scheduler.size());

	eth.moveTimeForwardAndInvokeEventsUs(3000);
	EXPECT_EQ(1, sparkCount);
	EXPECT_EQ(0, fallbackCount);
}

TEST(TriggerScheduler, cancelOnEmptyQueueIsSafe) {
	AngleBasedEvent event;
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engine->module<TriggerScheduler>()->cancel(&event);
	EXPECT_EQ(0, engine->module<TriggerScheduler>()->getQueueSizeForUnitTest());
}

TEST(TriggerScheduler, oldOverdwellPreservesRequeuedSparkAndDischargesCoil) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& scheduler = *engine->module<TriggerScheduler>();
	auto& ignition = engine->ignitionEvents.elements[0];
	auto& event = ignition.sparkEvent;
	engine->rpmCalculator.oneDegreeUs = 100;
	int newCount = 0;
	IgnitionContext oldContext;
	oldContext.eventIndex = 0;
	oldContext.outputsMask = 1;
	oldContext.isOverdwellProtect = true;

	scheduler.schedule(&event, EngPhase{123}, {countAction, &newCount});
	event.fallbackIsCurrent = true;
	engine->scheduler.schedule(
			"old overdwell",
			&event.scheduling,
			getTimeNowNt() + US2NT(2000),
			{fireSparkAndPrepareNextSchedule, oldContext});
	enginePins.coils[0].setHigh();

	scheduler.flush();
	EXPECT_FALSE(scheduler.scheduleOrQueue(&event, EngPhase{125}, {countAction, &newCount}, phaseAtCurrentTooth()));
	engine->ignitionState.sparkDwell = 2;
	engine->ignitionState.dwellAngle = 10;
	ignition.dwellAngle = 42;
	eth.moveTimeForwardAndInvokeEventsUs(2000);
	EXPECT_FALSE(enginePins.coils[0].getLogicValue());
	EXPECT_EQ(1, scheduler.getQueueSizeForUnitTest());
	EXPECT_EQ(42, ignition.dwellAngle);
	EXPECT_EQ(0, newCount);

	auto phase = phaseAtCurrentTooth();
	phase.currentTrgPhase = TrgPhase{120};
	phase.nextTrgPhase = TrgPhase{130};
	scheduler.onEnginePhase(1000, phase);
	eth.moveTimeForwardAndInvokeEventsUs(1000);
	EXPECT_EQ(1, newCount);
	EXPECT_EQ(0, scheduler.getQueueSizeForUnitTest());
	EXPECT_EQ(0, engine->scheduler.size());
}

TEST(TriggerScheduler, mixedQueuePromotesDueEventsAndPreservesWaitingOrder) {
	AngleBasedEvent events[6];
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& scheduler = *engine->module<TriggerScheduler>();
	engine->rpmCalculator.oneDegreeUs = 100;
	int fired[6] = {};
	for (int i = 0; i < 6; i++) {
		// Alternate waiting and due entries, so removal covers middle and tail links.
		scheduler.schedule(&events[i], EngPhase{i % 2 ? 105.f : 125.f}, {countAction, &fired[i]});
	}
	scheduler.onEnginePhase(1000, phaseAtCurrentTooth());
	ASSERT_EQ(3, scheduler.getQueueSizeForUnitTest());
	ASSERT_EQ(3, engine->scheduler.size());
	for (int i = 0; i < 3; i++) {
		EXPECT_EQ(&events[2 * i], scheduler.getElementAtIndexForUnitTest(i));
	}
	eth.moveTimeForwardAndInvokeEventsUs(1000);
	for (int i = 0; i < 6; i++) {
		EXPECT_EQ(i % 2, fired[i]);
	}

	auto phase = phaseAtCurrentTooth();
	phase.currentTrgPhase = TrgPhase{120};
	phase.nextTrgPhase = TrgPhase{130};
	scheduler.onEnginePhase(1000, phase);
	EXPECT_EQ(0, scheduler.getQueueSizeForUnitTest());
	eth.moveTimeForwardAndInvokeEventsUs(1000);
	for (auto count : fired) {
		EXPECT_EQ(1, count);
	}
}

TEST(TriggerScheduler, ignitionRegistersFallbackForEachNewCycle) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setCylinderCount(1);
	engineConfiguration->firingOrder = FO_1;
	engineConfiguration->minimumIgnitionTiming = -25;
	engine->rpmCalculator.oneDegreeUs = 100;
	engine->ignitionState.sparkDwell = 1;
	engine->ignitionState.dwellAngle = 10;
	engine->cylinders[0].setIgnitionTimingBtdc(-25);
	auto& scheduler = *engine->module<TriggerScheduler>();

	// Dwell at 15 degrees, spark at 25: firing waits for a later tooth.
	for (int cycle = 0; cycle < 2; cycle++) {
		onTriggerEventSparkLogic({getTimeNowNt(), 10, 20, 10, 20});
		ASSERT_EQ(1, scheduler.getQueueSizeForUnitTest());
		eth.moveTimeForwardAndInvokeEventsUs(500);
		EXPECT_TRUE(enginePins.coils[0].getLogicValue());
		eth.moveTimeForwardAndInvokeEventsUs(1500);
		EXPECT_FALSE(enginePins.coils[0].getLogicValue());
		EXPECT_EQ(0, scheduler.getQueueSizeForUnitTest());
		EXPECT_EQ(0, engine->scheduler.size());
	}
}

TEST(TriggerScheduler, overdwellRemovesPendingSpark) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto* triggerScheduler = &*engine->module<TriggerScheduler>();
	auto& event = engine->ignitionEvents.elements[0].sparkEvent;
	int sparkCount = 0;

	triggerScheduler->schedule(&event, EngPhase{123}, {countAction, &sparkCount});
	ASSERT_EQ(1, triggerScheduler->getQueueSizeForUnitTest());

	IgnitionContext ctx;
	ctx.eventIndex = 0;
	ctx.isOverdwellProtect = true;
	event.fallbackIsCurrent = true;
	engine->ignitionState.dwellAngle = NAN; // Stop after discharging the coil.
	fireSparkAndPrepareNextSchedule(ctx);

	EXPECT_EQ(0, triggerScheduler->getQueueSizeForUnitTest());
	EXPECT_EQ(0, sparkCount);
}

TEST(TriggerScheduler, cancelDuringPromotionPreventsLaterEvent) {
	InterleavingExecutor executor;
	AngleBasedEvent first;
	AngleBasedEvent second;
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto* triggerScheduler = &*engine->module<TriggerScheduler>();
	int fireCount = 0;

	executor.triggerScheduler = triggerScheduler;
	executor.victim = &second;
	engine->scheduler.setMockExecutor(&executor);
	triggerScheduler->schedule(&first, EngPhase{105}, {countAction, &fireCount});
	triggerScheduler->schedule(&second, EngPhase{106}, {countAction, &fireCount});
	triggerScheduler->onEnginePhase(1000, phaseAtCurrentTooth());

	ASSERT_EQ(1u, executor.scheduled.size());
	EXPECT_EQ(&first.scheduling, executor.scheduled[0]);
	EXPECT_EQ(0, triggerScheduler->getQueueSizeForUnitTest());
	engine->scheduler.setMockExecutor(nullptr);
}

TEST(TriggerScheduler, stopDuringPromotionDropsRemainingEvents) {
	InterleavingExecutor executor;
	AngleBasedEvent first;
	AngleBasedEvent second;
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto* triggerScheduler = &*engine->module<TriggerScheduler>();
	int fireCount = 0;

	executor.triggerScheduler = triggerScheduler;
	executor.stopOnSchedule = true;
	engine->scheduler.setMockExecutor(&executor);
	triggerScheduler->schedule(&first, EngPhase{105}, {countAction, &fireCount});
	triggerScheduler->schedule(&second, EngPhase{106}, {countAction, &fireCount});
	triggerScheduler->onEnginePhase(1000, phaseAtCurrentTooth());

	ASSERT_EQ(1u, executor.scheduled.size());
	EXPECT_EQ(&first.scheduling, executor.scheduled[0]);
	EXPECT_EQ(0, triggerScheduler->getQueueSizeForUnitTest());
	engine->scheduler.setMockExecutor(nullptr);
}

TEST(TriggerScheduler, cancelHeadMiddleTailAndAppendAfterLastRemoval) {
	AngleBasedEvent events[5];
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& scheduler = *engine->module<TriggerScheduler>();
	int count = 0;
	for (int index : {0, 1, 2, 3}) {
		scheduler.schedule(&events[index], EngPhase{125}, {countAction, &count});
	}
	for (int index : {0, 2, 3, 1}) {
		scheduler.cancel(&events[index]);
		EXPECT_EQ(TriggerQueueMembership::None, events[index].queueMembership);
		EXPECT_EQ(nullptr, events[index].next);
		ASSERT_TRUE(scheduler.validateQueuesForUnitTest());
	}
	scheduler.schedule(&events[4], EngPhase{125}, {countAction, &count});
	EXPECT_EQ(&events[4], scheduler.getElementAtIndexForUnitTest(0));
	EXPECT_EQ(1, scheduler.getQueueSizeForUnitTest());
	EXPECT_TRUE(scheduler.validateQueuesForUnitTest());
}

TEST(TriggerScheduler, flushResetsMembershipButPreservesFallbackAssociation) {
	AngleBasedEvent events[8];
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& scheduler = *engine->module<TriggerScheduler>();
	int count = 0;
	for (auto& event : events) {
		scheduler.schedule(&event, EngPhase{125}, {countAction, &count});
		event.fallbackIsCurrent = true;
	}
	scheduler.flush();
	for (auto& event : events) {
		EXPECT_EQ(TriggerQueueMembership::None, event.queueMembership);
		EXPECT_EQ(nullptr, event.next);
		EXPECT_TRUE(event.fallbackIsCurrent);
	}
	for (int index = 7; index >= 0; index--) {
		scheduler.schedule(&events[index], EngPhase{125}, {countAction, &count});
	}
	ASSERT_TRUE(scheduler.validateQueuesForUnitTest());
	for (int index = 0; index < 8; index++) {
		EXPECT_EQ(&events[7 - index], scheduler.getElementAtIndexForUnitTest(index));
	}
}

TEST(TriggerScheduler, duplicateInsertionPreservesMembershipAndTail) {
	AngleBasedEvent events[3];
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& scheduler = *engine->module<TriggerScheduler>();
	int count = 0;
	scheduler.schedule(&events[0], EngPhase{125}, {countAction, &count});
	scheduler.schedule(&events[1], EngPhase{125}, {countAction, &count});
	scheduler.schedule(&events[0], EngPhase{125}, {countAction, &count});
	scheduler.schedule(&events[2], EngPhase{125}, {countAction, &count});
	ASSERT_TRUE(scheduler.validateQueuesForUnitTest());
	EXPECT_EQ(3, scheduler.getQueueSizeForUnitTest());
	for (int i = 0; i < 3; i++) {
		EXPECT_EQ(&events[i], scheduler.getElementAtIndexForUnitTest(i));
	}
	EXPECT_EQ(1, eth.getWarningCounter());
}

TEST(TriggerScheduler, inlineDueCancellationAllowsReuseAndAppend) {
	InterleavingExecutor executor;
	AngleBasedEvent events[4];
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& scheduler = *engine->module<TriggerScheduler>();
	int count = 0;
	executor.triggerScheduler = &scheduler;
	executor.victim = &events[2];
	engine->scheduler.setMockExecutor(&executor);
	for (int i = 0; i < 3; i++) {
		scheduler.schedule(&events[i], EngPhase{105}, {countAction, &count});
	}
	scheduler.onEnginePhase(1000, phaseAtCurrentTooth());
	EXPECT_EQ(2u, executor.scheduled.size());
	ASSERT_TRUE(scheduler.validateQueuesForUnitTest());
	scheduler.schedule(&events[2], EngPhase{125}, {countAction, &count});
	scheduler.schedule(&events[3], EngPhase{125}, {countAction, &count});
	EXPECT_EQ(2, scheduler.getQueueSizeForUnitTest());
	EXPECT_TRUE(scheduler.validateQueuesForUnitTest());
	engine->scheduler.setMockExecutor(nullptr);
}

TEST(TriggerScheduler, inlineFlushClearsWaitingAndDueMembership) {
	InterleavingExecutor executor;
	AngleBasedEvent events[4];
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& scheduler = *engine->module<TriggerScheduler>();
	int count = 0;
	executor.triggerScheduler = &scheduler;
	executor.stopOnSchedule = true;
	engine->scheduler.setMockExecutor(&executor);
	for (int i = 0; i < 4; i++) {
		scheduler.schedule(&events[i], EngPhase{i % 2 ? 125.f : 105.f}, {countAction, &count});
	}
	scheduler.onEnginePhase(1000, phaseAtCurrentTooth());
	EXPECT_EQ(1u, executor.scheduled.size());
	ASSERT_TRUE(scheduler.validateQueuesForUnitTest());
	for (auto& event : events) {
		EXPECT_EQ(TriggerQueueMembership::None, event.queueMembership);
		scheduler.schedule(&event, EngPhase{125}, {countAction, &count});
	}
	EXPECT_EQ(4, scheduler.getQueueSizeForUnitTest());
	EXPECT_TRUE(scheduler.validateQueuesForUnitTest());
	engine->scheduler.setMockExecutor(nullptr);
}

TEST(TriggerScheduler, queueOperationsMatchReferenceAcrossRepeatedReuse) {
	AngleBasedEvent events[16];
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& scheduler = *engine->module<TriggerScheduler>();
	std::vector<int> expected;
	uint32_t state = 0x12345678;
	int count = 0;
	for (int step = 0; step < 2000; step++) {
		state = state * 1664525 + 1013904223;
		int index = (state >> 16) % 16;
		auto found = std::find(expected.begin(), expected.end(), index);
		switch ((state >> 24) % 4) {
			case 0:
				if (found == expected.end()) {
					scheduler.schedule(&events[index], EngPhase{125}, {countAction, &count});
					expected.push_back(index);
				}
				break;
			case 1:
				scheduler.cancel(&events[index]);
				if (found != expected.end()) {
					expected.erase(found);
				}
				break;
			case 2:
				scheduler.flush();
				expected.clear();
				break;
			case 3:
				scheduler.onEnginePhase(1000, phaseAtCurrentTooth());
				break;
		}
		ASSERT_TRUE(scheduler.validateQueuesForUnitTest()) << step;
		ASSERT_EQ(expected.size(), scheduler.getQueueSizeForUnitTest()) << step;
		for (size_t i = 0; i < expected.size(); i++) {
			EXPECT_EQ(&events[expected[i]], scheduler.getElementAtIndexForUnitTest(i));
		}
		for (int i = 0; i < 16; i++) {
			bool present = std::find(expected.begin(), expected.end(), i) != expected.end();
			EXPECT_EQ(
					present ? TriggerQueueMembership::Waiting : TriggerQueueMembership::None,
					events[i].queueMembership);
		}
	}
}
