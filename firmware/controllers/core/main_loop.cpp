#include "pch.h"

#include "periodic_thread_controller.h"
#include "electronic_throttle.h"

#if EFI_INTERNAL_FLASH && defined(HARDWARE_CI)
#include "flash_main.h"
#include "mpu_util.h"
#endif

#define MAIN_LOOP_RATE 1000

class MainLoop final : PeriodicController<1024> {
public:
	MainLoop();
	void PeriodicTask(efitick_t nowNt) override;

	void startMainLoop() {
		m_stallTimer.reset();
		startThread();
	}

private:
	template <LoopPeriod TFlag>
	LoopPeriod makePeriodFlag() const;

	LoopPeriod makePeriodFlags();

	int m_cycleCounter = 0;

	Timer m_stallTimer;

#if EFI_INTERNAL_FLASH && defined(HARDWARE_CI)
	uint32_t m_flashWriteSequence = 0;
#endif
};

static MainLoop mainLoop CCM_OPTIONAL;

void initMainLoop() {
	mainLoop.startMainLoop();
}

MainLoop::MainLoop()
	: PeriodicController("MainLoop", PRIO_MAIN_LOOP, MAIN_LOOP_RATE) {}

void MainLoop::PeriodicTask(efitick_t nowNt) {
	ScopePerf perf(PE::MainLoop);

	auto elapsedSinceLastLoop = m_stallTimer.getElapsedSecondsAndReset(nowNt);
	if (elapsedSinceLastLoop > 0.1) {
		efiPrintf("Main loop stall of %.3f sec detected", elapsedSinceLastLoop);

#if EFI_INTERNAL_FLASH && defined(HARDWARE_CI)
		// A chip that writes flash while running (from its own thread) must never stall for it. If
		// the sequence moved, or is odd, a flash write overlapped this stall. HW CI fails on this.
		uint32_t sequence = getFlashWriteSequence();
		if (allowFlashWhileRunning() && (sequence != m_flashWriteSequence || (sequence & 1))) {
			efiPrintf("HW CI: unexpected main loop stall of %.3f sec during a flash write", elapsedSinceLastLoop);
		}
#endif
	}
#if EFI_INTERNAL_FLASH && defined(HARDWARE_CI)
	m_flashWriteSequence = getFlashWriteSequence();
#endif

	LoopPeriod p = makePeriodFlags();

#if HAL_USE_ADC
	if (p & ADC_UPDATE_RATE) {
		updateSlowAdc(nowNt);
	}
#endif // HAL_USE_ADC

#if EFI_ELECTRONIC_THROTTLE_BODY
	if (p & ETB_UPDATE_RATE) {
		for (int i = 0; i < ETB_COUNT; i++) {
			auto etb = engine->etbControllers[i];

			if (etb) {
				etb->update();
			}
		}
	}
#endif // EFI_ELECTRONIC_THROTTLE_BODY

	if (p & SLOW_CALLBACK_RATE) {
		doPeriodicSlowCallback();
	}

	if (p & FAST_CALLBACK_RATE) {
		engine->periodicFastCallback();
	}
}

template <LoopPeriod flag>
static constexpr int loopCounts() {
	constexpr auto hz = hzForPeriod(flag);

	// check that this cleanly divides
	static_assert(MAIN_LOOP_RATE % hz == 0);

	return MAIN_LOOP_RATE / hz;
}

template <LoopPeriod TFlag>
LoopPeriod MainLoop::makePeriodFlag() const {
	if (m_cycleCounter % loopCounts<TFlag>() == 0) {
		return TFlag;
	} else {
		return LoopPeriod::None;
	}
}

LoopPeriod MainLoop::makePeriodFlags() {
	if (m_cycleCounter >= MAIN_LOOP_RATE) {
		m_cycleCounter = 0;
	}

	LoopPeriod lp = LoopPeriod::None;
	lp |= makePeriodFlag<LoopPeriod::Period1000hz>();
	lp |= makePeriodFlag<LoopPeriod::Period500hz>();
	lp |= makePeriodFlag<LoopPeriod::Period250hz>();
	lp |= makePeriodFlag<LoopPeriod::Period20hz>();

	m_cycleCounter++;

	return lp;
}
