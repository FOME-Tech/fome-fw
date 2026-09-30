/**
 * @file	signal_executor_sleep.cpp
 * @brief   Asynchronous output signal code
 *
 * Here we have the simplest, thread-based implementation of signal executor.
 * TODO: https://sourceforge.net/p/rusefi/tickets/6/
 *
 * @date Feb 10, 2013
 * @author Andrey Belomutskiy, (c) 2012-2020
 *
 * This file is part of rusEfi - see http://rusefi.com
 *
 * rusEfi is free software; you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * rusEfi is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 * even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include "pch.h"

#include "scheduler.h"
#include "main_trigger_callback.h"
#include <new>

#if EFI_SIGNAL_EXECUTOR_SLEEP

struct CallbackContext {
	scheduling_s* scheduling = nullptr;
	bool shouldFree = false;
};

static void doScheduleForLater(scheduling_s* scheduling, int delayUs, action_s action);

void SleepExecutor::schedule(const char* msg, scheduling_s* scheduling, efitick_t timeNt, action_s action) {
	doScheduleForLater(scheduling, NT2US(timeNt) - getTimeNowUs(), action);
}

static void timerCallback(CallbackContext* ctx) {
	// Grab the action but clear it in the event so we can reschedule from the action's execution
	action_s action = ctx->scheduling->action;
	ctx->scheduling->action = {};

	// Clean up any memory we allocated
	if (ctx->shouldFree) {
		delete ctx->scheduling;
	}
	delete ctx;

	// Lastly, actually execute the action
	action.execute();
}

bool SleepExecutor::scheduleBatch(const ScheduledAction* events, size_t count) {
	chibios_rt::CriticalSectionLocker csl;
	if (!isScheduleBatchValid(events, count, getTimeNowNt())) {
		return false;
	}

	CallbackContext* contexts[MaxScheduleBatchSize] = {};
	for (size_t i = 0; i < count; i++) {
		contexts[i] = new (std::nothrow) CallbackContext;
		if (contexts[i]) {
			contexts[i]->scheduling = new (std::nothrow) scheduling_s;
		}
		if (!contexts[i] || !contexts[i]->scheduling) {
			for (size_t j = 0; j <= i; j++) {
				if (contexts[j]) {
					delete contexts[j]->scheduling;
					delete contexts[j];
				}
			}
			return false;
		}
		contexts[i]->shouldFree = true;
		chVTObjectInit(&contexts[i]->scheduling->timer);
		contexts[i]->scheduling->action = events[i].action;
	}

	// Arm all future events before executing any already-due action.
	for (size_t i = 0; i < count; i++) {
		auto now = getTimeNowNt();
		int delaySt = events[i].time <= now ? 0 : MY_US2ST(NT2US(events[i].time - now));
		if (delaySt > 0) {
			chVTSetI(&contexts[i]->scheduling->timer, delaySt, (vtfunc_t)timerCallback, contexts[i]);
			contexts[i] = nullptr;
		}
	}
	for (size_t i = 0; i < count; i++) {
		if (contexts[i]) {
			timerCallback(contexts[i]);
		}
	}
	return true;
}

static void doScheduleForLater(scheduling_s* scheduling, int delayUs, action_s action) {
	int delaySt = MY_US2ST(delayUs);
	if (delaySt <= 0) {
		/**
		 * in case of zero delay, we should invoke the callback
		 */
		action.execute();
		return;
	}

	chibios_rt::CriticalSectionLocker csl;

	auto ctx = new CallbackContext;
	if (!scheduling) {
		scheduling = new scheduling_s;
		chVTObjectInit(&scheduling->timer);
		ctx->shouldFree = true;
	}
	ctx->scheduling = scheduling;

	scheduling->action = action;
	int isArmed = chVTIsArmedI(&scheduling->timer);
	if (isArmed) {
		/**
		 * timer reuse is normal for example in case of sudden RPM increase
		 */
		chVTResetI(&scheduling->timer);
	}

	chVTSetI(&scheduling->timer, delaySt, (vtfunc_t)timerCallback, ctx);
}

void SleepExecutor::cancel(scheduling_s* s) {
	chibios_rt::CriticalSectionLocker csl;

	if (chVTIsArmedI(&s->timer)) {
		chVTResetI(&s->timer);
	}

	s->action = {};
}

#endif /* EFI_SIGNAL_EXECUTOR_SLEEP */
