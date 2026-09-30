/**
 * @file	scheduler.h
 *
 * @date October 1, 2020
 */
#include "pch.h"

#include "scheduler.h"

void action_s::execute() {
	efiAssertVoid(ObdCode::CUSTOM_ERR_ASSERT, m_callback != NULL, "callback==null1");
	m_callback(m_param);
}

schfunc_t action_s::getCallback() const {
	return m_callback;
}

void* action_s::getArgument() const {
	return m_param;
}

bool isScheduleBatchValid(const ScheduledAction* events, size_t count, efitick_t now) {
	if (!events || count == 0 || count > MaxScheduleBatchSize) {
		return false;
	}

	for (size_t i = 0; i < count; i++) {
		// Unsigned distance avoids signed overflow for arbitrary rejected timestamps.
		uint64_t forward = static_cast<uint64_t>(events[i].time.count) - static_cast<uint64_t>(now.count);
		if (!events[i].action ||
			(events[i].time >= now && forward >= static_cast<uint64_t>(US2NT(MaximumScheduleDelayUs).count())) ||
			(i > 0 && events[i].time < events[i - 1].time)) {
			return false;
		}
	}

	return true;
}
