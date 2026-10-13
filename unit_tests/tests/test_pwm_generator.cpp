/*
 * test_pwm_generator.cpp
 *
 *  @date Dec 8, 2018
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"
#include "trigger_emulator_algo.h"

#include <vector>

#define LOW_VALUE 0
#define HIGH_VALUE 1
static int expectedTimeOfNextEvent;

static void assertNextEvent(const char* msg, int expectedPinState, TestExecutor* executor, OutputPin& pin) {
	printf("PWM_test: Asserting event [%s]\r\n", msg);
	// only one action expected in queue
	ASSERT_EQ(1, executor->size()) << "PWM_test: schedulingQueue size";

	// move time to next event timestamp
	setTimeNowUs(expectedTimeOfNextEvent);

	// execute pending actions and assert that only one action was executed
	ASSERT_EQ(1, executor->executeAll(getTimeNowUs())) << msg << " executed";
	ASSERT_EQ(expectedPinState, pin.m_currentLogicValue) << msg << " pin state";

	// assert that we have one new action in queue
	ASSERT_EQ(1, executor->size()) << "PWM_test: queue.size";
}

TEST(PWM, test100dutyCycle) {
	printf("*************************************** test100dutyCycle\r\n");

	expectedTimeOfNextEvent = 0;
	setTimeNowUs(0);

	OutputPin pin;
	SimplePwm pwm("test PWM1");
	TestExecutor executor;

	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engine->scheduler.setMockExecutor(&executor);

	startSimplePwm(&pwm, "unit_test", &pin, 1000 /* frequency */, 1.0 /* duty cycle */);

	expectedTimeOfNextEvent += 1000;
	EXPECT_EQ(expectedTimeOfNextEvent, executor.getForUnitTest(0)->momentX);

	assertNextEvent("exec@100", HIGH_VALUE, &executor, pin);

	expectedTimeOfNextEvent += 1000;
	assertNextEvent("exec2@100", HIGH_VALUE, &executor, pin);

	expectedTimeOfNextEvent += 1000;
	assertNextEvent("exec3@100", HIGH_VALUE, &executor, pin);
}

TEST(PWM, testSwitchToNanPeriod) {
	expectedTimeOfNextEvent = 0;
	setTimeNowUs(0);

	OutputPin pin;
	SimplePwm pwm("test PWM1");
	TestExecutor executor;

	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engine->scheduler.setMockExecutor(&executor);

	startSimplePwm(&pwm, "unit_test", &pin, 1000 /* frequency */, 0.60 /* duty cycle */);

	expectedTimeOfNextEvent += 600;
	EXPECT_EQ(expectedTimeOfNextEvent, executor.getForUnitTest(0)->momentX);

	assertNextEvent("exec@70", LOW_VALUE, &executor, pin);
	ASSERT_EQ(600, getTimeNowUs()) << "time1";

	expectedTimeOfNextEvent += 400;
	assertNextEvent("exec2@70", HIGH_VALUE, &executor, pin);

	pwm.setFrequency(NAN);

	expectedTimeOfNextEvent += 600;
	EXPECT_EQ(expectedTimeOfNextEvent, executor.getForUnitTest(0)->momentX);
	assertNextEvent("exec2@NAN", LOW_VALUE, &executor, pin);

	expectedTimeOfNextEvent += MS2US(NAN_FREQUENCY_SLEEP_PERIOD_MS);
	EXPECT_EQ(expectedTimeOfNextEvent, executor.getForUnitTest(0)->momentX);
	assertNextEvent("exec3@NAN", LOW_VALUE, &executor, pin);
}

TEST(PWM, testPwmGenerator) {
	expectedTimeOfNextEvent = 0;
	setTimeNowUs(0);

	OutputPin pin;
	SimplePwm pwm("test PWM3");
	TestExecutor executor;

	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engine->scheduler.setMockExecutor(&executor);

	startSimplePwm(&pwm, "unit_test", &pin, 1000 /* frequency */, 0.80 /* duty cycle */);

	expectedTimeOfNextEvent += 800;
	EXPECT_EQ(expectedTimeOfNextEvent, executor.getForUnitTest(0)->momentX);

	assertNextEvent("exec@0", LOW_VALUE, &executor, pin);
	ASSERT_EQ(800, getTimeNowUs()) << "time1";

	expectedTimeOfNextEvent += 200;
	EXPECT_EQ(expectedTimeOfNextEvent, executor.getForUnitTest(0)->momentX);

	// above we had vanilla duty cycle, now let's handle a special case
	pwm.setSimplePwmDutyCycle(0);
	EXPECT_EQ(expectedTimeOfNextEvent, executor.getForUnitTest(0)->momentX);

	assertNextEvent("exec@1", LOW_VALUE, &executor, pin);
	ASSERT_EQ(1000, getTimeNowUs()) << "time2";

	expectedTimeOfNextEvent += 1000;
	EXPECT_EQ(expectedTimeOfNextEvent, executor.getForUnitTest(0)->momentX);

	assertNextEvent("exec@2", LOW_VALUE /* pin value */, &executor, pin);
	ASSERT_EQ(2000, getTimeNowUs()) << "time3";
	expectedTimeOfNextEvent += 1000;
	EXPECT_EQ(expectedTimeOfNextEvent, executor.getForUnitTest(0)->momentX);

	assertNextEvent("exec@3", LOW_VALUE /* pin value */, &executor, pin);
	ASSERT_EQ(3000, getTimeNowUs()) << "time4";
	expectedTimeOfNextEvent += 1000;
	EXPECT_EQ(expectedTimeOfNextEvent, executor.getForUnitTest(0)->momentX);

	assertNextEvent("exec@4", LOW_VALUE /* pin value */, &executor, pin);
	expectedTimeOfNextEvent += 1000;
	EXPECT_EQ(expectedTimeOfNextEvent, executor.getForUnitTest(0)->momentX);

	assertNextEvent("exec@5", LOW_VALUE /* pin value */, &executor, pin);
	expectedTimeOfNextEvent += 1000;
	ASSERT_EQ(5000, getTimeNowUs()) << "time4";
	EXPECT_EQ(expectedTimeOfNextEvent, executor.getForUnitTest(0)->momentX);

	assertNextEvent("exec@6", LOW_VALUE /* pin value */, &executor, pin);
}

namespace {
class RecordingPwm : public SimplePwm {
public:
	std::vector<efitimeus_t> timestamps;
	bool feedTrigger = false;
	TriggerEmulatorHelper emulator;

	static void onPhase(int stateIndex, PwmConfig* state, efitick_t timestamp) {
		auto& pwm = *static_cast<RecordingPwm*>(state);
		pwm.timestamps.push_back(NT2US(timestamp));
		if (pwm.feedTrigger) {
			pwm.emulator.handleEmulatorCallback(*pwm.multiChannelStateSequence, stateIndex, timestamp);
		}
	}
};

class PwmTimestampTest : public testing::Test {
protected:
	RecordingPwm pwm;
	// Destroy the queue before the PWM object whose scheduling storage it contains.
	TestExecutor executor;
	EngineTestHelper eth{engine_type_e::TEST_ENGINE};

	void SetUp() override {
		engine->scheduler.setMockExecutor(&executor);
		pwm.seq.setSwitchTime(0, 0.5f);
		pwm.seq.setSwitchTime(1, 1);
		pwm.seq.setChannelState(0, 0, false);
		pwm.seq.setChannelState(0, 1, true);
		pwm.setFrequency(1000);
		setTimeNowUs(10000);
		start();
	}

	void start() {
		pwm.weComplexInit(&pwm.seq, nullptr, RecordingPwm::onPhase);
	}

	void runAt(efitimeus_t timeUs) {
		setTimeNowUs(timeUs);
		executor.executeAll(getTimeNowUs());
	}
};
} // namespace

TEST_F(PwmTimestampTest, delayedCallbacksKeepScheduledTimes) {
	ASSERT_EQ((std::vector<efitimeus_t>{10000}), pwm.timestamps);
	runAt(11400);
	EXPECT_EQ((std::vector<efitimeus_t>{10000, 10500, 11000}), pwm.timestamps);
	ASSERT_EQ(1, executor.size());
	EXPECT_EQ(11500, executor.getHead()->momentX);
}

TEST_F(PwmTimestampTest, frequencyChangesKeepScheduledCycleBoundary) {
	runAt(10500);
	pwm.setFrequency(2000);
	runAt(11100); // Cycle boundary at 11000, executed 100 us late.
	EXPECT_EQ(11000, pwm.timestamps.back());
	ASSERT_EQ(1, executor.size());
	EXPECT_EQ(11250, executor.getHead()->momentX);
	runAt(11250);
	pwm.setFrequency(500);
	runAt(11600); // Boundary at 11500 must also survive a decrease in frequency.
	EXPECT_EQ(11500, pwm.timestamps.back());
	EXPECT_EQ(12500, executor.getHead()->momentX);
}

TEST_F(PwmTimestampTest, precisionResetKeepsScheduledCycleBoundary) {
	// Cross the periodic floating-point precision reset, including a late cycle boundary.
	for (int phase = 1; phase <= 1800; phase++) {
		runAt(10000 + phase * 500 + 100);
		ASSERT_EQ(10000 + phase * 500, pwm.timestamps.back()) << "phase " << phase;
	}
	EXPECT_EQ(1801u, pwm.timestamps.size());
}

TEST_F(PwmTimestampTest, restartUsesCurrentTimeInsteadOfOldDeadline) {
	runAt(10500);
	pwm.stop();
	runAt(11000);
	ASSERT_EQ(0, executor.size());
	ASSERT_EQ(2u, pwm.timestamps.size());
	setTimeNowUs(20000);
	start();
	EXPECT_EQ(20000, pwm.timestamps.back());
	ASSERT_EQ(1, executor.size());
	EXPECT_EQ(20500, executor.getHead()->momentX);
}

TEST_F(PwmTimestampTest, resumeAfterNanPeriodDoesNotMoveTimestampBackwards) {
	pwm.setFrequency(NAN);
	runAt(10500);
	pwm.setFrequency(2000);
	runAt(110600);
	// The paused phase is delivered, then the old cycle is abandoned at current time.
	EXPECT_EQ((std::vector<efitimeus_t>{10000, 10500, 110500, 110600}), pwm.timestamps);
	ASSERT_EQ(1, executor.size());
	EXPECT_EQ(110850, executor.getHead()->momentX);
}

TEST_F(PwmTimestampTest, longStallRebasesWithoutDrainingOldCycles) {
	runAt(40000);
	EXPECT_EQ((std::vector<efitimeus_t>{10000, 10500, 40000}), pwm.timestamps);
	ASSERT_EQ(1, executor.size());
	EXPECT_EQ(40500, executor.getHead()->momentX);
}

TEST_F(PwmTimestampTest, immediateDutyChangesUseCurrentTime) {
	setTimeNowUs(10123);
	pwm.setSimplePwmDutyCycle(0);
	setTimeNowUs(10234);
	pwm.setSimplePwmDutyCycle(1);
	EXPECT_EQ((std::vector<efitimeus_t>{10000, 10123, 10234}), pwm.timestamps);
}

static void checkDelayedTrigger(trigger_type_e trigger) {
	RecordingPwm pwm;
	TestExecutor executor;
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->skippedWheelOnCam = false;
	eth.setTriggerType(trigger);
	engineConfiguration->isInjectionEnabled = false;
	engineConfiguration->isIgnitionEnabled = false;

	engine->scheduler.setMockExecutor(&executor);
	pwm.feedTrigger = true;
	pwm.setFrequency(100); // Crank wheel at 6000 RPM.
	setTimeNowUs(10000);
	pwm.weComplexInit(&engine->triggerCentral.triggerShape.wave, nullptr, RecordingPwm::onPhase);

	for (int event = 0; event < 1200; event++) {
		ASSERT_GT(executor.size(), 0);
		// Once synchronized, delay the timer enough to require several catch-up callbacks.
		efitimeus_t delay = event > 200 && event % 97 == 0 ? 600 : 0;
		setTimeNowUs(executor.getHead()->momentX + delay);
		executor.executeAll(getTimeNowUs());
	}

	EXPECT_EQ(0u, engine->triggerCentral.triggerState.triggerErrorCounter);
	EXPECT_EQ(0u, engine->triggerCentral.triggerState.orderingErrorCounter);
	EXPECT_TRUE(engine->triggerCentral.triggerState.getShaftSynchronized());
	EXPECT_NEAR(6000, engine->rpmCalculator.getCachedRpm(), 10);
}

TEST(TriggerStimulatorTiming, delayedVw60_2) {
	checkDelayedTrigger(trigger_type_e::TT_60_2_VW);
}

TEST(TriggerStimulatorTiming, delayed60_2) {
	checkDelayedTrigger(trigger_type_e::TT_TOOTHED_WHEEL_60_2);
}
