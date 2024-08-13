#include "pch.h"

#include "periodic_thread_controller.h"

#define MAIN_LOOP_RATE 1000

class MainLoop : public PeriodicController<512> {
public:
	MainLoop();
	void PeriodicTask(efitick_t nowNt) override;

private:
	template <LoopPeriod TFlag>
	LoopPeriod makePeriodFlag() const;

	LoopPeriod makePeriodFlags();

	int m_cycleCounter = 0;
};

static MainLoop mainLoop;

void initMainLoop() {
	mainLoop.start();
}

MainLoop::MainLoop()
	: PeriodicController("MainLoop", PRIO_MAIN_LOOP, MAIN_LOOP_RATE)
{
}

void MainLoop::PeriodicTask(efitick_t nowNt) {
	LoopPeriod p = makePeriodFlags();

	if (p & ADC_UPDATE_RATE) {
		updateSlowAdc(nowNt);
	}

	if (p & FAST_CALLBACK_RATE) {
		engine->periodicFastCallback();
	}

	if (p & SLOW_CALLBACK_RATE) {
		doPeriodicSlowCallback();
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
