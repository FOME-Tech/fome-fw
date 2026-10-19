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
	bool validateQueuesForUnitTest() const;

private:
	struct Queue {
		AngleBasedEvent* head = nullptr;
		AngleBasedEvent* tail = nullptr;
	};

	void schedule(AngleBasedEvent* event, action_s action);
	// Helpers require the scheduler critical section. Timers are managed separately.
	static void append(Queue& queue, AngleBasedEvent* event, TriggerQueueMembership membership);
	static void unlink(Queue& queue, AngleBasedEvent* event, AngleBasedEvent* previous);
	static void clear(Queue& queue);

	Queue m_waiting;
	// Keep due events reachable during inline callbacks from timer registration.
	Queue m_due;
};
