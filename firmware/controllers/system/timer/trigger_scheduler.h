#pragma once

class TriggerScheduler : public EngineModule {
public:
	void schedule(AngleBasedEvent* event, EngPhase angle, action_s action);

	bool scheduleOrQueue(AngleBasedEvent* event, EngPhase angle, action_s action, const EnginePhaseInfo& phase);

	void onEnginePhase(float rpm, const EnginePhaseInfo& phase) override;
	void onEngineStop() override;
	// Remove an event that must no longer wait for a trigger tooth (for example, after overdwell fires).
	void cancel(AngleBasedEvent* event);

	/**
	 * Forget every pending angle-based event, because the schedule they belong to is gone -
	 * the engine stopped, or the trigger configuration changed underneath us. Without this
	 * the entries would sit in the queue until their angle came around again, which for a
	 * stopped engine is never.
	 *
	 * This only drops the queue entries. Any timer already armed on an event (overdwell
	 * protection, in particular) is deliberately left running so it can still de-energize
	 * a coil that is currently charging.
	 */
	void flush();

	// For unit tests
	AngleBasedEvent* getElementAtIndexForUnitTest(int index);
	int getQueueSizeForUnitTest() const;

private:
	void schedule(AngleBasedEvent* event, action_s action);

	bool assertNotInList(AngleBasedEvent* head, AngleBasedEvent* element);

	/**
	 * That's the linked list of pending events scheduled in relation to trigger
	 * At the moment we iterate over the whole list while looking for events for specific
	 * trigger index We can make it an array of lists per trigger index, but that would take
	 * some RAM and probably not needed yet.
	 */
	AngleBasedEvent* m_angleBasedEventsHead = nullptr;
	// Due events stay visible to cancel() while they are transferred to the time scheduler.
	AngleBasedEvent* m_dueEventsHead = nullptr;
};
