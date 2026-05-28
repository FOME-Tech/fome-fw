/*
 * @file engine_controller_misc.cpp
 *
 * @date Mar 22, 2020
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"

#include "cpu_usage.h"

extern ButtonDebounce startStopButtonDebounce;

static uint8_t nextThreadId = 0;
void threadInitHook(void* vtp) {
	// No lock required, this is already under lock
	auto tp = reinterpret_cast<thread_t*>(vtp);
	tp->threadId = ++nextThreadId;
}

void irqEnterHook() {
	cpuUsageOnIsrEnter();
#if ENABLE_PERF_TRACE
	perfEventBegin(PE::ISR);
#endif
}

void irqExitHook() {
#if ENABLE_PERF_TRACE
	perfEventEnd(PE::ISR);
#endif
	cpuUsageOnIsrExit();
}

void contextSwitchHook() {
#if ENABLE_PERF_TRACE
	perfEventInstantGlobal(PE::ContextSwitch);
#endif
}

void onIdleEnterHook() {
	cpuUsageOnIdleEnter();
#if ENABLE_PERF_TRACE
	perfEventBegin(PE::Idle);
#endif
}

void onIdleExitHook() {
#if ENABLE_PERF_TRACE
	perfEventEnd(PE::Idle);
#endif
	cpuUsageOnIdleExit();
}

static void onStartStopButtonToggle() {
	engine->engineState.startStopStateToggleCounter++;

	if (engine->rpmCalculator.isStopped()) {
		bool wasStarterEngaged = enginePins.starterControl.getAndSet(1);
		if (!wasStarterEngaged) {
			engine->startStopStateLastPushTime = getTimeNowNt();
			efiPrintf(
					"Let's crank this engine for up to %d seconds via %s!",
					engineConfiguration->startCrankingDuration,
					hwPortname(engineConfiguration->starterControlPin));
		}
	} else if (engine->rpmCalculator.isRunning()) {
		efiPrintf("Let's stop this engine!");
		doScheduleStopEngine();
	}
}

void slowStartStopButtonCallback() {
	bool startStopState = startStopButtonDebounce.readPinEvent();

	if (startStopState && !engine->engineState.startStopState) {
		// we are here on transition from 0 to 1
		onStartStopButtonToggle();
	}
	engine->engineState.startStopState = startStopState;

	if (engine->startStopStateLastPushTime == 0) {
		// nothing is going on with startStop button
		return;
	}

	// TODO: split starter (/disable relay) control in to its own controller
	if (engine->rpmCalculator.isRunning()) {
		// turn starter off once engine is running
		bool wasStarterEngaged = enginePins.starterControl.getAndSet(0);
		if (wasStarterEngaged) {
			efiPrintf("Engine runs we can disengage the starter");
			engine->startStopStateLastPushTime = 0;
		}
	}

	if (getTimeNowNt() - engine->startStopStateLastPushTime >
		NT_PER_SECOND * engineConfiguration->startCrankingDuration) {
		bool wasStarterEngaged = enginePins.starterControl.getAndSet(0);
		if (wasStarterEngaged) {
			efiPrintf("Cranking timeout %d seconds", engineConfiguration->startCrankingDuration);
			engine->startStopStateLastPushTime = 0;
		}
	}
}
