/**
 * @file event_queue.cpp
 * This is a data structure which keeps track of all pending events
 * Implemented as a linked list, which is fine since the number of
 * pending events is pretty low
 * todo: MAYBE migrate to a better data structure, but that's low priority
 *
 * this data structure is NOT thread safe
 *
 * @date Apr 17, 2014
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"

#include "event_queue.h"
#include "efitime.h"

#if EFI_UNIT_TEST
extern int timeNowUs;
extern bool verboseMode;
#endif /* EFI_UNIT_TEST */

EventQueue::EventQueue(efitick_t lateDelay)
	: lateDelay(lateDelay)
{
	for (size_t i = 0; i < efi::size(m_pool); i++) {
		tryReturnScheduling(&m_pool[i]);
	}
}

scheduling_s* EventQueue::getFreeScheduling() {
	auto retVal = m_freelist;

	if (retVal) {
		m_freelist = retVal->nextScheduling_s;
		retVal->nextScheduling_s = nullptr;

#if EFI_PROD_CODE
		getTunerStudioOutputChannels()->schedulingUsedCount++;
#endif
	}

	return retVal;
}

void EventQueue::tryReturnScheduling(scheduling_s* sched) {
	// Only return this scheduling to the free list if it's from the correct pool
	if (sched >= &m_pool[0] && sched <= &m_pool[efi::size(m_pool) - 1]) {
		sched->nextScheduling_s = m_freelist;
		m_freelist = sched;

#if EFI_PROD_CODE
		getTunerStudioOutputChannels()->schedulingUsedCount--;
#endif
	}
}

/**
 * @return true if inserted into the head of the list
 */
bool EventQueue::insertTask(scheduling_s *scheduling, efitick_t timeX, action_s action) {
	ScopePerf perf(PE::EventQueueInsertTask);

	if (!scheduling) {
		scheduling = getFreeScheduling();

		// If still null, the free list is empty and all schedulings in the pool have been expended.
		if (!scheduling) {
			// TODO: should we warn or error here?

			return false;
		}
	}

#if EFI_UNIT_TEST
	assertListIsSorted();
#endif /* EFI_UNIT_TEST */
	efiAssert(ObdCode::CUSTOM_ERR_ASSERT, action.getCallback() != NULL, "NULL callback", false);

// please note that simulator does not use this code at all - simulator uses signal_executor_sleep

	if (scheduling->action) {
#if EFI_UNIT_TEST
		if (verboseMode) {
			printf("Already scheduled was %d\r\n", (int)scheduling->momentX);
			printf("Already scheduled now %d\r\n", (int)timeX);
		}
#endif /* EFI_UNIT_TEST */
		return false;
	}

	scheduling->momentX = timeX;
	scheduling->action = action;

	if (head == NULL || timeX < head->momentX) {
		// here we insert into head of the linked list
		LL_PREPEND2(head, scheduling, nextScheduling_s);
#if EFI_UNIT_TEST
		assertListIsSorted();
#endif /* EFI_UNIT_TEST */
		return true;
	} else {
		// here we know we are not in the head of the list, let's find the position - linear search
		scheduling_s *insertPosition = head;
		while (insertPosition->nextScheduling_s != NULL && insertPosition->nextScheduling_s->momentX < timeX) {
			insertPosition = insertPosition->nextScheduling_s;
		}

		scheduling->nextScheduling_s = insertPosition->nextScheduling_s;
		insertPosition->nextScheduling_s = scheduling;
#if EFI_UNIT_TEST
		assertListIsSorted();
#endif /* EFI_UNIT_TEST */
		return false;
	}
}

void EventQueue::remove(scheduling_s* scheduling) {
#if EFI_UNIT_TEST
		assertListIsSorted();
#endif /* EFI_UNIT_TEST */

	// Special case: event isn't scheduled, so don't cancel it
	if (!scheduling->action) {
		return;
	}

	// Special case: empty list, nothing to do
	if (!head) {
		return;
	}

	// Special case: is the item to remove at the head?
	if (scheduling == head) {
		head = head->nextScheduling_s;
		scheduling->nextScheduling_s = nullptr;
		scheduling->action = {};
	} else {
		auto prev = head;	// keep track of the element before the one to remove, so we can link around it
		auto current = prev->nextScheduling_s;

		// Find our element
		while (current && current != scheduling) {
			prev = current;
			current = current->nextScheduling_s;
		}

		// Walked off the end, this is an error since this *should* have been scheduled
		if (!current) {
			firmwareError(ObdCode::OBD_PCM_Processor_Fault, "EventQueue::remove didn't find element");
			return;
		}

		efiAssertVoid(ObdCode::OBD_PCM_Processor_Fault, current == scheduling, "current not equal to scheduling");

		// Link around the removed item
		prev->nextScheduling_s = current->nextScheduling_s;

		// Clean the item to remove
		current->nextScheduling_s = nullptr;
		current->action = {};
	}

#if EFI_UNIT_TEST
	assertListIsSorted();
#endif /* EFI_UNIT_TEST */
}

/**
 * On this layer it does not matter which units are used - us, ms ot nt.
 *
 * This method is always invoked under a lock
 * @return Get the timestamp of the soonest pending action, skipping all the actions in the past
 */
expected<efitick_t> EventQueue::getNextEventTime(efitick_t nowX) const {
	if (head != NULL) {
		if (head->momentX <= nowX) {
			/**
			 * We are here if action timestamp is in the past. We should rarely be here since this 'getNextEventTime()' is
			 * always invoked by 'scheduleTimerCallback' which is always invoked right after 'executeAllPendingActions' - but still,
			 * for events which are really close to each other we would end up here.
			 *
			 * looks like we end up here after 'writeconfig' (which freezes the firmware) - we are late
			 * for the next scheduled event
			 */
			return nowX + lateDelay;
		} else {
			return head->momentX;
		}
	}

	return unexpected;
}

/**
 * See also maxPrecisionCallbackDuration for total hw callback time
 */
uint32_t maxEventCallbackDuration = 0;

/**
 * Invoke all pending actions prior to specified timestamp
 * @return number of executed actions
 */
int EventQueue::executeAll(efitick_t now) {
	ScopePerf perf(PE::EventQueueExecuteAll);

	int executionCounter = 0;

#if EFI_UNIT_TEST
	assertListIsSorted();
#endif

	bool didExecute;
	do {
		didExecute = executeOne(now);
		executionCounter += didExecute ? 1 : 0;
	} while (didExecute);

	return executionCounter;
}

bool EventQueue::executeOne(efitick_t now) {
	// Read the head every time - a previously executed event could
	// have inserted something new at the head
	scheduling_s* current = head;

	// Queue is empty - bail
	if (!current) {
		return false;
	}

	// If the next event is far in the future, we'll reschedule
	// and execute it next time.
	// We do this when the next event is close enough that the overhead of
	// resetting the timer and scheduling an new interrupt is greater than just
	// waiting for the time to arrive.  On current CPUs, this is reasonable to set
	// around 10 microseconds.
	if (current->momentX > now + lateDelay) {
		return false;
	}

	// near future - spin wait for the event to happen and avoid the
	// overhead of rescheduling the timer.
	// yes, that's a busy wait but that's what we need here
	while (current->momentX > getTimeNowNt()) {
		UNIT_TEST_BUSY_WAIT_CALLBACK();
	}

	// step the head forward, unlink this element, clear scheduled flag
	head = current->nextScheduling_s;
	current->nextScheduling_s = nullptr;

	// Grab the action but clear it in the event so we can reschedule from the action's execution
	auto action = current->action;
	current->action = {};

	tryReturnScheduling(current);
	current = nullptr;

#if EFI_UNIT_TEST
	printf("QUEUE: execute current=%d param=%d\r\n", (uintptr_t)current, (uintptr_t)action.getArgument());
#endif

	// Execute the current element
	{
		ScopePerf perf2(PE::EventQueueExecuteCallback);
		action.execute();
	}

#if EFI_UNIT_TEST
	// (tests only) Ensure we didn't break anything
	assertListIsSorted();
#endif

	return true;
}

int EventQueue::size(void) const {
	scheduling_s *tmp;
	int result;
	LL_COUNT2(head, tmp, result, nextScheduling_s);
	return result;
}

void EventQueue::assertListIsSorted() const {
	scheduling_s *current = head;
	while (current != NULL && current->nextScheduling_s != NULL) {
		efiAssertVoid(ObdCode::CUSTOM_ERR_6623, current->momentX <= current->nextScheduling_s->momentX, "list order");
		current = current->nextScheduling_s;
	}
}

scheduling_s * EventQueue::getHead() {
	return head;
}

// todo: reduce code duplication with another 'getElementAtIndexForUnitText'
scheduling_s *EventQueue::getElementAtIndexForUnitText(int index) {
	scheduling_s * current;

	LL_FOREACH2(head, current, nextScheduling_s)
	{
		if (index == 0)
			return current;
		index--;
	}

	return NULL;
}

void EventQueue::clear(void) {
	// Flush the queue, resetting all scheduling_s as though we'd executed them
	while(head) {
		auto x = head;
		// link next element to head
		head = x->nextScheduling_s;

		// Reset this element
		x->momentX = 0;
		x->nextScheduling_s = nullptr;
		x->action = {};
	}

	head = nullptr;
}
