#include "pch.h"

#include "event_queue.h"

#include "utlist.h"

bool TriggerScheduler::assertNotInList(AngleBasedEvent* head, AngleBasedEvent* element) {
	/* this code is just to validate state, no functional load*/
	decltype(head) current;
	int counter = 0;
	LL_FOREACH2(head, current, next) {
		if (++counter > QUEUE_LENGTH_LIMIT) {
			firmwareError(ObdCode::CUSTOM_ERR_LOOPED_QUEUE, "Looped queue?");
			return false;
		}

		if (current == element) {
			/**
			 * for example, this might happen in case of sudden RPM change if event
			 * was not scheduled by angle but was scheduled by time. In case of scheduling
			 * by time with slow RPM the whole next fast revolution might be within the wait
			 */
			warning(ObdCode::CUSTOM_RE_ADDING_INTO_EXECUTION_QUEUE, "re-adding element into event_queue");
			return true;
		}
	}

	return false;
}

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

void TriggerScheduler::schedule(AngleBasedEvent* event, action_s action) {
	// Both public scheduling entry points hold the lock while updating the event.
	event->action = action;

	// TODO: This is O(n), consider some other way of detecting if in a list,
	// and consider doubly linked or other list tricks.

	if (!assertNotInList(m_angleBasedEventsHead, event) && !assertNotInList(m_dueEventsHead, event)) {
		// Use Append to retain some semblance of event ordering in case of
		// time skew.  Thus on events are always followed by off events.
		LL_APPEND2(m_angleBasedEventsHead, event, next);
	}
}

void TriggerScheduler::cancel(AngleBasedEvent* event) {
	chibios_rt::CriticalSectionLocker csl;

	// LL_DELETE2 dereferences an empty head, even if the event is not in the list.
	if (m_angleBasedEventsHead) {
		LL_DELETE2(m_angleBasedEventsHead, event, next);
	}
	if (m_dueEventsHead) {
		LL_DELETE2(m_dueEventsHead, event, next);
	}
	event->next = nullptr;
}

void TriggerScheduler::flush() {
	chibios_rt::CriticalSectionLocker csl;

	// Do not cancel armed time-based events: overdwell must still be able to turn a coil off.
	m_angleBasedEventsHead = nullptr;
	m_dueEventsHead = nullptr;
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

		AngleBasedEvent** dueTail = &m_dueEventsHead;
		while (*dueTail) {
			dueTail = &(*dueTail)->next;
		}

		// Unlink through the previous link, without searching from the head for every due event.
		auto** link = &m_angleBasedEventsHead;
		while (auto* current = *link) {
			if (current->shouldSchedule(phase)) {
				*link = current->next;
				current->next = nullptr;
				*dueTail = current;
				dueTail = &current->next;
			} else {
				link = &current->next;
			}
		}

		// Avoid another lock/unlock on the common empty or not-yet-due path.
		if (!m_dueEventsHead) {
			return;
		}
	}

	// A timer callback may run while scheduleByAngle() is called. Keep every other
	// due event in a member list so cancel() and flush() can still find it.
	while (true) {
		chibios_rt::CriticalSectionLocker csl;
		auto* current = m_dueEventsHead;
		if (!current) {
			break;
		}
		m_dueEventsHead = current->next;
		current->next = nullptr;

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
// todo: reduce code duplication with another 'getElementAtIndexForUnitText'
AngleBasedEvent* TriggerScheduler::getElementAtIndexForUnitTest(int index) {
	AngleBasedEvent* current;

	LL_FOREACH2(m_angleBasedEventsHead, current, next) {
		if (index == 0) {
			return current;
		}
		index--;
	}
	firmwareError("getElementAtIndexForUnitText: null");
	return nullptr;
}

int TriggerScheduler::getQueueSizeForUnitTest() const {
	int count = 0;

	AngleBasedEvent* current;
	LL_FOREACH2(m_angleBasedEventsHead, current, next) {
		count++;
	}
	LL_FOREACH2(m_dueEventsHead, current, next) {
		count++;
	}

	return count;
}
#endif /* EFI_UNIT_TEST */
