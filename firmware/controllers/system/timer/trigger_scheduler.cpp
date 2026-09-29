#include "pch.h"

#include "event_queue.h"

void TriggerScheduler::schedule(AngleBasedEvent* event, EngPhase angle, action_s action) {
	chibios_rt::CriticalSectionLocker csl;
	event->fallbackIsCurrent = false;
	event->setAngle(angle);

	schedule(event, action);
}

/**
 * Schedules 'action' to occur at engine cycle angle 'angle'.
 *
 * @return true if event corresponds to current tooth and was time-based scheduler
 *         false if event was put into queue for scheduling at a later tooth
 */
bool TriggerScheduler::scheduleOrQueue(
		AngleBasedEvent* event, EngPhase angle, action_s action, const EnginePhaseInfo& phase) {
	chibios_rt::CriticalSectionLocker csl;
	event->fallbackIsCurrent = false;
	event->setAngle(angle);

	if (event->shouldSchedule(phase)) {
		// A previous cycle may have left this event waiting for a tooth, with an
		// overdwell timer armed on the same scheduling record. Replace both.
		cancel(event);
		if (event->scheduling.action) {
			engine->scheduler.cancel(&event->scheduling);
		}

		// if we're due now, just schedule the event
		scheduleByAngle(&event->scheduling, phase.timestamp, event->getAngleFromNow(phase), action);

		return true;
	} else {
		// If not due now, add it to the queue to be scheduled later
		schedule(event, action);

		return false;
	}
}

void TriggerScheduler::append(Queue& queue, AngleBasedEvent* event, TriggerQueueMembership membership) {
	event->next = nullptr;
	if (queue.tail) {
		queue.tail->next = event;
	} else {
		queue.head = event;
	}
	queue.tail = event;
	event->queueMembership = membership;
}

void TriggerScheduler::unlink(Queue& queue, AngleBasedEvent* event, AngleBasedEvent* previous) {
	if (previous) {
		previous->next = event->next;
	} else {
		queue.head = event->next;
	}
	if (queue.tail == event) {
		queue.tail = previous;
	}
	event->next = nullptr;
	event->queueMembership = TriggerQueueMembership::None;
}

void TriggerScheduler::schedule(AngleBasedEvent* event, action_s action) {
	// Both public entry points hold the lock while updating the event.
	event->action = action;
	if (event->queueMembership != TriggerQueueMembership::None) {
		warning(ObdCode::CUSTOM_RE_ADDING_INTO_EXECUTION_QUEUE, "re-adding element into event_queue");
		return;
	}
	append(m_waiting, event, TriggerQueueMembership::Waiting);
}

void TriggerScheduler::cancel(AngleBasedEvent* event) {
	chibios_rt::CriticalSectionLocker csl;
	if (event->queueMembership == TriggerQueueMembership::None) {
		return;
	}
	auto& queue = event->queueMembership == TriggerQueueMembership::Waiting ? m_waiting : m_due;
	AngleBasedEvent* previous = nullptr;
	auto* current = queue.head;
	int count = 0;
	while (current && current != event) {
		if (++count > QUEUE_LENGTH_LIMIT) {
			firmwareError(ObdCode::CUSTOM_ERR_LOOPED_QUEUE, "Looped trigger queue");
			return;
		}
		previous = current;
		current = current->next;
	}
	if (!current) {
		firmwareError(ObdCode::CUSTOM_ERR_LOOPED_QUEUE, "Trigger queue membership mismatch");
		return;
	}
	unlink(queue, event, previous);
}

void TriggerScheduler::clear(Queue& queue) {
	int count = 0;
	while (auto* event = queue.head) {
		if (++count > QUEUE_LENGTH_LIMIT) {
			firmwareError(ObdCode::CUSTOM_ERR_LOOPED_QUEUE, "Looped trigger queue");
			return;
		}
		unlink(queue, event, nullptr);
	}
}

void TriggerScheduler::flush() {
	chibios_rt::CriticalSectionLocker csl;
	// Reset membership before events can be reused. Preserve armed timers and their fallback association.
	clear(m_waiting);
	clear(m_due);
}

void TriggerScheduler::onEngineStop() {
	flush();
}

void TriggerScheduler::onEnginePhase(float rpm, const EnginePhaseInfo& phase) {
	if (rpm == 0 || !EFI_SHAFT_POSITION_INPUT) {
		// this might happen for instance in case of a single trigger event after a pause
		return;
	}

	{
		chibios_rt::CriticalSectionLocker csl;

		AngleBasedEvent* previous = nullptr;
		auto* current = m_waiting.head;
		int count = 0;
		while (current) {
			if (++count > QUEUE_LENGTH_LIMIT) {
				firmwareError(ObdCode::CUSTOM_ERR_LOOPED_QUEUE, "Looped trigger queue");
				return;
			}
			auto* next = current->next;
			if (current->shouldSchedule(phase)) {
				unlink(m_waiting, current, previous);
				append(m_due, current, TriggerQueueMembership::Due);
			} else {
				previous = current;
			}
			current = next;
		}

		// Avoid another lock/unlock on the common empty or not-yet-due path.
		if (!m_due.head) {
			return;
		}
	}

	// A timer callback may run while scheduleByAngle() is called. Keep every other
	// due event in a member list so cancel() and flush() can still find it.
	while (true) {
		chibios_rt::CriticalSectionLocker csl;
		auto* current = m_due.head;
		if (!current) {
			break;
		}
		unlink(m_due, current, nullptr);

		// Keep promotion atomic with engine stop and cancellation on another thread.
		// Replace a possible overdwell timer with the actual event time.
		engine->scheduler.cancel(&current->scheduling);
		scheduleByAngle(&current->scheduling, phase.timestamp, current->getAngleFromNow(phase), current->action);
	}
}

void AngleBasedEvent::setAngle(EngPhase angle) {
	eventPhase = getTriggerCentral()->toTrgPhase(angle);
}

bool AngleBasedEvent::shouldSchedule(const EnginePhaseInfo& phase) const {
	return isPhaseInRange(eventPhase, phase);
}

float AngleBasedEvent::getAngleFromNow(const EnginePhaseInfo& phase) const {
	float angleOffset = eventPhase - phase.currentTrgPhase;
	if (angleOffset < 0) {
		angleOffset += engine->engineState.engineCycle;
	}

	return angleOffset;
}

#if EFI_UNIT_TEST
AngleBasedEvent* TriggerScheduler::getElementAtIndexForUnitTest(int index) {
	for (auto* current = m_waiting.head; current; current = current->next) {
		if (index-- == 0) {
			return current;
		}
	}
	firmwareError("getElementAtIndexForUnitTest: null");
	return nullptr;
}

int TriggerScheduler::getQueueSizeForUnitTest() const {
	int count = 0;
	for (const auto* queue : {&m_waiting, &m_due}) {
		for (auto* current = queue->head; current; current = current->next) {
			if (++count > QUEUE_LENGTH_LIMIT) {
				return -1;
			}
		}
	}
	return count;
}

bool TriggerScheduler::validateQueuesForUnitTest() const {
	for (const auto* queue : {&m_waiting, &m_due}) {
		auto membership = queue == &m_waiting ? TriggerQueueMembership::Waiting : TriggerQueueMembership::Due;
		AngleBasedEvent* previous = nullptr;
		int count = 0;
		for (auto* current = queue->head; current; current = current->next) {
			if (++count > QUEUE_LENGTH_LIMIT || current->queueMembership != membership) {
				return false;
			}
			previous = current;
		}
		if (previous != queue->tail) {
			return false;
		}
	}
	return true;
}
#endif
