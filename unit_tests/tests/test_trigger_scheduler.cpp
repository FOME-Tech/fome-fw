/*
 * @file test_trigger_scheduler.cpp
 *
 * Lifecycle of the angle-based event queue. Entries are only ever drained when their angle comes
 * around on a tooth, so anything that stops the engine or schedules an event by time instead has
 * to take its entry back out - otherwise it lingers until something re-adds it and trips
 * CUSTOM_RE_ADDING_INTO_EXECUTION_QUEUE.
 */

#include "pch.h"

static void doNothing(void*) {}

TEST(TriggerScheduler, engineStopEmptiesQueue) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	AngleBasedEvent event;

	// Queue an event at an angle we won't reach, so it just sits there
	engine->module<TriggerScheduler>()->schedule(&event, EngPhase{123}, {doNothing, nullptr});
	ASSERT_EQ(1, engine->module<TriggerScheduler>()->getQueueSizeForUnitTest());

	// The engine stopping means that angle is never coming - the entry has to go
	engine->OnTriggerSynchronizationLost();
	ASSERT_EQ(0, engine->module<TriggerScheduler>()->getQueueSizeForUnitTest());
}

TEST(TriggerScheduler, scheduleByTimeRemovesStaleQueueEntry) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	AngleBasedEvent event;

	// A previous cycle left this event queued
	engine->module<TriggerScheduler>()->schedule(&event, EngPhase{123}, {doNothing, nullptr});
	ASSERT_EQ(1, engine->module<TriggerScheduler>()->getQueueSizeForUnitTest());

	// Now the same event is scheduled again, but this time it's due within the current tooth, so
	// it goes to the time-based scheduler instead of the queue
	EnginePhaseInfo phase;
	phase.timestamp = getTimeNowNt();
	phase.currentEngPhase = EngPhase{100};
	phase.nextEngPhase = EngPhase{110};
	phase.currentTrgPhase = TrgPhase{100};
	phase.nextTrgPhase = TrgPhase{110};

	bool scheduled =
			engine->module<TriggerScheduler>()->scheduleOrQueue(&event, EngPhase{105}, {doNothing, nullptr}, phase);

	ASSERT_TRUE(scheduled);

	// The stale entry must be gone, otherwise onEnginePhase would later cancel the timer we just
	// armed and re-fire the old action in its place
	ASSERT_EQ(0, engine->module<TriggerScheduler>()->getQueueSizeForUnitTest());
}
