/*
 * @file test_periodic_thread_controller.cpp
 *
 * May 14, 2021
 * @author Andrey Belomutskiy, (c) 2012-2021
 */

#include "pch.h"

systime_t chVTGetSystemTime(void) {
	return getTimeNowUs();
}

systime_t chThdSleepUntilWindowed(systime_t prev, systime_t next) {
	return 0;
}

bool chThdShouldTerminateX(void) {
	return false;
}
