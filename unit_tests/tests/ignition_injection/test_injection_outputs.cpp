/**
 * @file test_injection_outputs.cpp
 *
 * What the injectors really do in each injection mode, and while the amount of fuel changes, checked
 * at the injector pins with a simulated engine (see harness/engine_sim.h).
 */

#include "pch.h"

#include "harness/engine_invariants.h"

using ::testing::_;

extern WarningCodeState unitTestWarningCodeState;

namespace {

constexpr float testRpm = 3000;
// One engine cycle at testRpm
constexpr int64_t cycleUs = 40000;

// A four cylinder engine running steadily in the given injection mode, with a trigger that knows
// engine phase by itself
struct InjectionRig {
	// configure runs after the engine is set up and before the simulated engine is created, for anything
	// that has to be there from the start (a different trigger, a cam input, the timing mode)
	explicit InjectionRig(
			injection_mode_e mode,
			trigger_type_e trigger = trigger_type_e::TT_ONE,
			std::function<void(EngineTestHelper&)> configure = {}) {
		EXPECT_CALL(*eth.mockAirmass, getAirmass(_, _)).WillRepeatedly([this](float, bool) {
			return AirmassResult{airmass, 50.0f};
		});

		// End of injection half a revolution before each cylinder's own TDC
		setTable(config->injectionPhase, -180.0f);
		// The trigger wheel is on the cam, so it knows engine phase by itself
		setCamOperationMode();
		setupSimpleTestEngineWithMaf(&eth, mode, trigger);

		if (configure) {
			configure(eth);
		}

		sim = std::make_unique<EngineSim>(eth, &recorder);
		sim->setRpm(testRpm);
		sim->runCycles(10);
	}

	// Run some more cycles, returning when they started
	int64_t run(int cycles) {
		auto from = sim->nowUs();
		sim->runCycles(cycles);
		return from;
	}

	// Set the fuel so that each sequential pulse is this long
	void setPulseMs(float ms) {
		airmass = airmassFor3ms * ms / 3;
	}

	// Pulses of one injector that started at or after a point in time
	std::vector<OutputPulse> pulsesSince(int injector, int64_t fromUs) const {
		std::vector<OutputPulse> result;

		for (const auto& pulse : recorder.pulses(OutputKind::Injector, injector)) {
			if (pulse.startUs >= fromUs) {
				result.push_back(pulse);
			}
		}

		return result;
	}

	// Fuel each injector delivers per engine cycle, as open time, checking that it's steady
	float openTimePerCycle(int injector, int64_t fromUs, int cycles) const {
		auto windows = activeTimePerWindow(recorder, OutputKind::Injector, injector, fromUs, cycleUs, cycles);

		for (float window : windows) {
			EXPECT_NEAR(windows[0], window, 0.01f) << "injector " << injector << " isn't steady";
		}

		return windows[0];
	}

	EngineTestHelper eth{engine_type_e::TEST_ENGINE};
	// Air mass per cylinder that makes a 3 ms sequential pulse (there's no deadtime)
	static constexpr float airmassFor3ms = 0.1008f;
	float airmass = airmassFor3ms;
	OutputRecorder recorder;
	std::unique_ptr<EngineSim> sim;
};

// Whether two injectors open at the same moments
bool openTogether(const InjectionRig& rig, int a, int b, int64_t fromUs) {
	auto pulsesA = rig.pulsesSince(a, fromUs);
	auto pulsesB = rig.pulsesSince(b, fromUs);

	if (pulsesA.empty() || pulsesA.size() != pulsesB.size()) {
		return false;
	}

	for (size_t i = 0; i < pulsesA.size(); i++) {
		if (pulsesA[i].startUs != pulsesB[i].startUs || pulsesA[i].endUs != pulsesB[i].endUs) {
			return false;
		}
	}

	return true;
}

// Sequential mode only: how many pulses each injector's intakes got, checking that it was one each.
//
// A pulse counts towards the first intake that ends at or after it starts, where an intake "ends" at
// the configured end of injection, 180 degrees before that cylinder's TDC. That way a pulse that started
// from an out of date angle still counts towards the intake it was meant for. The first and last intakes
// seen are left out, since they may be only partly inside the time recorded.
::testing::AssertionResult onePulsePerIntake(const InjectionRig& rig, int64_t fromUs) {
	constexpr double endOfInjection = 720 - 180;

	std::stringstream problems;
	int problemCount = 0;

	for (int cyl = 0; cyl < 4; cyl++) {
		std::map<int64_t, int> pulsesByIntake;
		std::optional<double> firstIntake;

		for (const auto& pulse : rig.pulsesSince(cyl, fromUs)) {
			double phase = rig.sim->enginePhase(pulse.startAngle) - engine->cylinders[cyl].getAngleOffset();
			double toIntake = std::fmod(endOfInjection - phase + 4 * 720, 720.0);
			double intake = pulse.startAngle + toIntake;

			if (!firstIntake) {
				firstIntake = intake;
			}

			pulsesByIntake[std::llround((intake - *firstIntake) / 720)]++;
		}

		if (pulsesByIntake.size() < 3) {
			return ::testing::AssertionFailure() << "injector " << cyl << " barely fired";
		}

		int64_t last = pulsesByIntake.rbegin()->first;
		for (int64_t intake = 1; intake < last; intake++) {
			int count = pulsesByIntake[intake];
			if (count != 1 && problemCount++ < 10) {
				problems << "\n  injector " << cyl << ", intake " << intake << " got " << count << " pulses";
			}
		}
	}

	if (problemCount) {
		return ::testing::AssertionFailure()
			<< problemCount << " intakes didn't get exactly one pulse:" << problems.str();
	}

	return ::testing::AssertionSuccess();
}

// Sequential mode only: every intake got fuel. An intake is the cycle up to its cylinder's end of injection
// (180 degrees before TDC); each must have had the injector open for at least minDeg of it.
//
// Unlike onePulsePerIntake this doesn't count pulses, so it also accepts a late pulse that ran in to the
// next one and merged with it at the pin. It only fails when an intake is skipped altogether.
::testing::AssertionResult fuelInEveryIntake(const InjectionRig& rig, int64_t fromUs, double minDeg) {
	constexpr double endOfInjection = 720 - 180;

	std::stringstream problems;
	int problemCount = 0;

	for (int cyl = 0; cyl < 4; cyl++) {
		auto pulses = rig.pulsesSince(cyl, fromUs);
		if (pulses.size() < 3) {
			return ::testing::AssertionFailure() << "injector " << cyl << " barely fired";
		}

		// The first end of injection after the first pulse starts, then every cycle until the last pulse
		double phase = rig.sim->enginePhase(pulses.front().startAngle) - engine->cylinders[cyl].getAngleOffset();
		double firstAnchor = pulses.front().startAngle + std::fmod(endOfInjection - phase + 4 * 720, 720.0);

		// Leave out the last intake, which may still be in progress
		for (double anchor = firstAnchor + 720; anchor < pulses.back().startAngle; anchor += 720) {
			double open = 0;
			for (const auto& pulse : pulses) {
				open += std::max(0.0, std::min(pulse.endAngle, anchor) - std::max(pulse.startAngle, anchor - 720));
			}

			if (open < minDeg && problemCount++ < 10) {
				problems << "\n  injector " << cyl << ", intake ending at " << anchor << ": open " << open
						 << " degrees";
			}
		}
	}

	if (problemCount) {
		return ::testing::AssertionFailure() << problemCount << " intakes got too little fuel:" << problems.str();
	}

	return ::testing::AssertionSuccess();
}

void doNothing(void*) {}

// Crank degrees between the starts of consecutive pulses of one injector, from fromUs on
std::vector<double> startSpacings(const InjectionRig& rig, int injector, int64_t fromUs) {
	std::vector<double> result;
	auto pulses = rig.pulsesSince(injector, fromUs);
	for (size_t i = 1; i < pulses.size(); i++) {
		result.push_back(pulses[i].startAngle - pulses[i - 1].startAngle);
	}
	return result;
}

// Every injector's pulses started between minDeg and maxDeg of crank apart, from fromUs on
::testing::AssertionResult startSpacingsWithin(const InjectionRig& rig, int64_t fromUs, double minDeg, double maxDeg) {
	std::stringstream problems;
	int count = 0;

	for (int cyl = 0; cyl < 4; cyl++) {
		auto spacings = startSpacings(rig, cyl, fromUs);
		if (spacings.size() < 3) {
			return ::testing::AssertionFailure() << "injector " << cyl << " barely fired";
		}
		for (double spacing : spacings) {
			if ((spacing < minDeg || spacing > maxDeg) && count++ < 10) {
				problems << "\n  injector " << cyl << ": " << spacing << " degrees between pulses";
			}
		}
	}

	if (count) {
		return ::testing::AssertionFailure()
			<< count << " pulse spacings outside " << minDeg << " to " << maxDeg << problems.str();
	}

	return ::testing::AssertionSuccess();
}

// The first event in firing order is cylinder 1's, which drives injector 0 in sequential mode
const InjectionEvent& firstEvent() {
	return engine->injectionEvents.elements[0];
}

} // namespace

TEST(InjectionOutputs, Sequential) {
	InjectionRig rig(IM_SEQUENTIAL);
	const int cycles = 5;
	auto from = rig.run(cycles);

	EXPECT_EQ(IM_SEQUENTIAL, getCurrentInjectionMode());

	float pulseMs = engine->engineState.injectionDuration;
	EXPECT_GT(pulseMs, 1);

	for (int cyl = 0; cyl < 4; cyl++) {
		// One pulse per cycle, of the duration the firmware says it's injecting
		auto pulses = rig.pulsesSince(cyl, from);
		ASSERT_EQ(pulses.size(), (size_t)cycles) << "injector " << cyl;

		for (const auto& pulse : pulses) {
			EXPECT_NEAR(pulseMs, pulse.durationMs(), 0.01f) << "injector " << cyl;

			// Each cylinder's injection ends at the same point in its own cycle: the configured
			// 180 degrees before its own TDC
			double endPhase = rig.sim->enginePhase(pulse.endAngle) - engine->cylinders[cyl].getAngleOffset();
			EXPECT_NEAR(-180, std::remainder(endPhase, 720.0), 0.5) << "injector " << cyl;
		}

		EXPECT_NEAR(pulseMs, rig.openTimePerCycle(cyl, from, cycles), 0.01f) << "injector " << cyl;
	}

	// No injector shares its timing with another
	for (int other = 1; other < 4; other++) {
		EXPECT_FALSE(openTogether(rig, 0, other, from)) << "injectors 0 and " << other;
	}
}

// Batch mode fires each injector together with the one a revolution away from it in the firing order,
// which is also what makes it work with injectors wired in pairs ("two wire batch")
TEST(InjectionOutputs, Batch) {
	InjectionRig rig(IM_BATCH);
	const int cycles = 5;
	auto from = rig.run(cycles);

	EXPECT_EQ(IM_BATCH, getCurrentInjectionMode());

	// Firing order is 1-3-4-2: cylinders 1 and 4 are a pair, and so are 2 and 3
	EXPECT_TRUE(openTogether(rig, 0, 3, from));
	EXPECT_TRUE(openTogether(rig, 1, 2, from));
	EXPECT_FALSE(openTogether(rig, 0, 1, from));

	float pulseMs = engine->engineState.injectionDuration;

	for (int cyl = 0; cyl < 4; cyl++) {
		// Two pulses per cycle
		auto pulses = rig.pulsesSince(cyl, from);
		ASSERT_EQ(pulses.size(), (size_t)2 * cycles) << "injector " << cyl;

		for (const auto& pulse : pulses) {
			EXPECT_NEAR(pulseMs, pulse.durationMs(), 0.01f) << "injector " << cyl;
		}

		EXPECT_NEAR(2 * pulseMs, rig.openTimePerCycle(cyl, from, cycles), 0.01f) << "injector " << cyl;
	}
}

TEST(InjectionOutputs, Simultaneous) {
	InjectionRig rig(IM_SIMULTANEOUS);
	const int cycles = 5;
	auto from = rig.run(cycles);

	EXPECT_EQ(IM_SIMULTANEOUS, getCurrentInjectionMode());

	// Everything fires together, once per cylinder's worth of rotation
	for (int cyl = 1; cyl < 4; cyl++) {
		EXPECT_TRUE(openTogether(rig, 0, cyl, from)) << "injectors 0 and " << cyl;
	}

	EXPECT_EQ(rig.pulsesSince(0, from).size(), (size_t)4 * cycles);
}

TEST(InjectionOutputs, SinglePoint) {
	InjectionRig rig(IM_SINGLE_POINT);
	const int cycles = 5;
	auto from = rig.run(cycles);

	EXPECT_EQ(IM_SINGLE_POINT, getCurrentInjectionMode());

	// The one injector fires for every cylinder...
	EXPECT_EQ(rig.pulsesSince(0, from).size(), (size_t)4 * cycles);

	// ...and the others not at all
	for (int cyl = 1; cyl < 4; cyl++) {
		EXPECT_TRUE(rig.pulsesSince(cyl, from).empty()) << "injector " << cyl;
	}
}

// However the fuel is divided up in to pulses, each cylinder gets the same amount per cycle
TEST(InjectionOutputs, SameFuelInEveryMode) {
	const int cycles = 5;

	float expected;
	{
		InjectionRig sequential(IM_SEQUENTIAL);
		expected = sequential.openTimePerCycle(0, sequential.run(cycles), cycles);
	}
	EXPECT_GT(expected, 1);

	for (auto mode : {IM_BATCH, IM_SIMULTANEOUS}) {
		InjectionRig rig(mode);
		auto from = rig.run(cycles);

		for (int cyl = 0; cyl < 4; cyl++) {
			EXPECT_NEAR(expected, rig.openTimePerCycle(cyl, from, cycles), 0.01f * expected)
					<< "injector " << cyl << " in mode " << (int)mode;
		}
	}

	// Single point feeds all four cylinders from one injector
	InjectionRig singlePoint(IM_SINGLE_POINT);
	EXPECT_NEAR(4 * expected, singlePoint.openTimePerCycle(0, singlePoint.run(cycles), cycles), 0.04f * expected);
}

// Change the fuel per cycle (as ms of injector open time) and check that every injector delivers
// the right amount on the way there and once it has settled.
//
// On the way, each injector fires at most one more old-sized cycle before switching, so over a number
// of cycles it delivers between "one old cycle then all new" and "all new". This is checked as a
// total, not cycle by cycle, because the start of injection moves when the duration changes, which
// shifts fuel between one cycle's window and the next without changing how much there is.
static void stepFuel(InjectionRig& rig, float baseAirmass, float basePerCycle, float fromPerCycle, float toPerCycle) {
	rig.airmass = baseAirmass * toPerCycle / basePerCycle;

	const int cycles = 6;
	auto changing = rig.run(cycles);

	float allNew = cycles * toPerCycle;
	float oneOld = fromPerCycle + (cycles - 1) * toPerCycle;

	for (int cyl = 0; cyl < 4; cyl++) {
		float total = 0;
		for (float window : activeTimePerWindow(rig.recorder, OutputKind::Injector, cyl, changing, cycleUs, cycles)) {
			total += window;
		}

		EXPECT_GE(total, 0.99f * std::min(allNew, oneOld))
				<< "injector " << cyl << " changing from " << fromPerCycle << " to " << toPerCycle << " ms per cycle";
		EXPECT_LE(total, 1.01f * std::max(allNew, oneOld))
				<< "injector " << cyl << " changing from " << fromPerCycle << " to " << toPerCycle << " ms per cycle";
	}

	auto settled = rig.run(4);
	EXPECT_TRUE(injectedPerWindowWithin(rig.recorder, settled, cycleUs, 4, 0.99f * toPerCycle, 1.01f * toPerCycle))
			<< "settled at " << toPerCycle << " ms per cycle";
}

static void stopAndCheckNothingLeftOpen(InjectionRig& rig) {
	rig.sim->setRpm(0);
	rig.sim->runSeconds(1);

	EXPECT_TRUE(allOutputsOff());
	for (int cyl = 0; cyl < 4; cyl++) {
		EXPECT_EQ(0, enginePins.injectors[cyl].getOverlappingCounter()) << "injector " << cyl;
	}

	EXPECT_EQ(0, unitTestWarningCodeState.recentWarnings.getCount());
}

// Run through a series of fuel amounts, starting from wherever the rig is now
static void stepFuelThrough(InjectionRig& rig, std::initializer_list<float> steps) {
	const float basePerCycle = rig.openTimePerCycle(0, rig.run(4), 4);
	const float baseAirmass = rig.airmass;

	float previous = basePerCycle;
	for (float perCycle : steps) {
		stepFuel(rig, baseAirmass, basePerCycle, previous, perCycle);
		previous = perCycle;
	}

	// ...and back to where it started
	stepFuel(rig, baseAirmass, basePerCycle, previous, basePerCycle);
}

// Pulse width stepping up and down, through to 87.5% duty where one batch pulse runs in to the next.
// However the pulses overlap or merge, every injector must deliver what was asked for, and none may
// be left open.
// https://github.com/rusefi/rusefi/issues/299
TEST(InjectionOutputs, BatchFuelFollowsPulseWidthChanges) {
	InjectionRig rig(IM_BATCH);

	stepFuelThrough(rig, {6, 12, 25, 35, 25, 12, 4});

	EXPECT_TRUE(noStuckInjector(rig.recorder, 36));
	stopAndCheckNothingLeftOpen(rig);
}

TEST(InjectionOutputs, SequentialFuelFollowsPulseWidthChanges) {
	InjectionRig rig(IM_SEQUENTIAL);

	stepFuelThrough(rig, {6, 12, 6, 4});

	EXPECT_TRUE(noStuckInjector(rig.recorder, 13));
	stopAndCheckNothingLeftOpen(rig);
}

// The same at high duty on a trigger with a tooth edge only every 360 degrees: when the pulse gets longer,
// its start can move back past the last tooth, and it must start late rather than be skipped
TEST(InjectionOutputs, SequentialFuelFollowsPulseWidthChangesAtHighDuty) {
	InjectionRig rig(IM_SEQUENTIAL);

	stepFuelThrough(rig, {6, 12, 25, 35, 25, 12});

	stopAndCheckNothingLeftOpen(rig);
}

// The same in batch mode, with the 1.5 ms to 12.5 ms pulse width change of the original issue 299 tests
TEST(InjectionOutputs, BatchFuelFollowsLargePulseWidthStep) {
	InjectionRig rig(IM_BATCH);

	stepFuelThrough(rig, {25});

	stopAndCheckNothingLeftOpen(rig);
}

// A big step up in fuel: 25 to 35 ms, 62% to 87% duty, on a 60-2 wheel on the cam (a tooth every 12 degrees
// of crank), at every point in the cycle. The pulse that should have started before the change has to start
// late, but no intake may be skipped. At this duty a late pulse can still be open when the next one starts,
// and they merge at the pin, so this checks that every intake got fuel rather than counting pulses.
TEST(InjectionOutputs, EveryIntakeGetsFuelWhenDutyJumpsOnFineWheel) {
	InjectionRig rig(IM_SEQUENTIAL, trigger_type_e::TT_TOOTHED_WHEEL_60_2);
	rig.setPulseMs(25);
	rig.run(4);
	auto from = rig.sim->nowUs();

	// Make the change at different points in the cycle, so it catches each cylinder at each stage
	for (int k = 0; k < 8; k++) {
		// Coming back down from 87% to 62% is fine
		rig.setPulseMs(25);
		rig.run(3);
		rig.sim->runDegrees(37 + 90 * k);
		rig.setPulseMs(35);
		rig.run(3);
	}

	// The shortest pulse is 25 ms, 450 degrees
	EXPECT_TRUE(fuelInEveryIntake(rig, from, 400));
}

// The other way: the pulse more than halving, at every point in the cycle. The pulse already committed with
// the longer duration's earlier start mustn't be followed by a second one in the same cycle at the shorter
// duration's later start.
TEST(InjectionOutputs, EveryIntakeGetsOnePulseWhenPulseHalvesOnFineWheel) {
	InjectionRig rig(IM_SEQUENTIAL, trigger_type_e::TT_TOOTHED_WHEEL_60_2);
	rig.setPulseMs(25);
	rig.run(4);
	auto from = rig.sim->nowUs();

	for (int k = 0; k < 8; k++) {
		// Back up in steps small enough not to skip anything
		for (float pulseMs : {12, 20, 25}) {
			rig.setPulseMs(pulseMs);
			rig.run(2);
		}
		rig.sim->runDegrees(37 + 90 * k);
		rig.setPulseMs(6);
		rig.run(3);
	}

	EXPECT_TRUE(onePulsePerIntake(rig, from));
}

// Smaller steps, the same way, are fine: 30% to 50% duty and back
TEST(InjectionOutputs, EveryIntakeGetsOnePulseThroughModerateStepsOnFineWheel) {
	InjectionRig rig(IM_SEQUENTIAL, trigger_type_e::TT_TOOTHED_WHEEL_60_2);
	rig.setPulseMs(12);
	rig.run(4);
	auto from = rig.sim->nowUs();

	for (int k = 0; k < 8; k++) {
		rig.setPulseMs(12);
		rig.run(3);
		rig.sim->runDegrees(37 + 90 * k);
		rig.setPulseMs(20);
		rig.run(3);
	}

	EXPECT_TRUE(onePulsePerIntake(rig, from));
	stopAndCheckNothingLeftOpen(rig);
}

// Injection takes its timer slots from a pool shared by everything that schedules without its own
// storage. The open, the close and the stage 2 close each take one. If there aren't enough left for all
// of a pulse's events, the pulse must be skipped: an open that got the last slot without its close would
// leave the injector open with nothing to close it.
TEST(InjectionOutputs, InjectorNotLeftOpenWhenSchedulerRunsOutOfSlots) {
	InjectionRig rig(IM_SEQUENTIAL);
	float pulseMs = engine->engineState.injectionDuration;

	// Wait for a moment when nothing is scheduled, so every slot in the pool is free
	while (engine->scheduler.size() != 0) {
		rig.sim->runDegrees(1);
	}

	// Take all but one of the 64 slots for the next 100 ms
	for (int i = 0; i < 63; i++) {
		engine->scheduler.schedule("test", nullptr, getTimeNowNt() + MS2NT(100), {doNothing});
	}
	ASSERT_EQ(63, engine->scheduler.size());

	auto from = rig.run(10);

	// A pulse is 3 ms. Skipping some is better than leaving one open.
	EXPECT_TRUE(noStuckInjector(rig.recorder, 4, from));

	// Running out is reported
	EXPECT_GT(unitTestWarningCodeState.recentWarnings.getCount(), 0);

	// Once the slots are free again, every injector is back to normal
	auto after = rig.run(4);
	for (int cyl = 0; cyl < 4; cyl++) {
		EXPECT_NEAR(pulseMs, rig.openTimePerCycle(cyl, after, 4), 0.01f) << "injector " << cyl;
	}
}

// The injector output counts opens and closes so that overlapping pulses merge, and only goes low when the
// count gets back to zero. If a close were ever lost (here the injector is opened once more than the
// scheduler does), the injector would stay open until the engine stopped. When the watchdog then forces it
// off, it must forget the count too: otherwise, on the next start, every open would just move the count
// from one to two and that injector would never open at all.
TEST(InjectionOutputs, InjectorWorksAfterRestartFollowingLostClose) {
	InjectionRig rig(IM_SEQUENTIAL);
	float pulseMs = engine->engineState.injectionDuration;

	// Unit tests don't normally assign the injectors to pins, and the watchdog only turns off outputs
	// that have one
	const brain_pin_e pins[] = {Gpio::E0, Gpio::E1, Gpio::E2, Gpio::E3};
	for (int cyl = 0; cyl < 4; cyl++) {
		enginePins.injectors[cyl].initPin("injector", pins[cyl]);
	}

	enginePins.injectors[0].open();
	rig.run(2);

	// With so few teeth it takes the watchdog a couple of seconds to decide the engine has stopped
	rig.sim->setRpm(0);
	rig.sim->runSeconds(3);
	EXPECT_TRUE(allOutputsOff());
	EXPECT_EQ(0, enginePins.injectors[0].getOverlappingCounter());

	rig.sim->setRpm(testRpm);
	rig.run(10);

	auto from = rig.run(4);
	for (int cyl = 0; cyl < 4; cyl++) {
		EXPECT_NEAR(pulseMs, rig.openTimePerCycle(cyl, from, 4), 0.01f) << "injector " << cyl;
	}
}

// While fuel is cut (zero fuel mass, as in decel fuel cut) there are no pulses to place. The first pulse
// after the cut must be placed from the fuel then, not from a pulse of no length during the cut, which would
// make it start at the end of injection angle and so end a whole pulse late.
//
// On a fine wheel it may still end up to a tooth late: if fuel comes back just after the tooth on which the
// pulse would have had to start, it starts late on the next one rather than not at all.
TEST(InjectionOutputs, FirstPulseAfterFuelCutEndsOnTime) {
	InjectionRig rig(IM_SEQUENTIAL, trigger_type_e::TT_TOOTHED_WHEEL_60_2);
	rig.setPulseMs(6);
	rig.run(4);

	rig.setPulseMs(0);
	rig.run(3);

	// Back to the same fuel as before
	rig.setPulseMs(6);
	auto from = rig.run(3);

	for (int cyl = 0; cyl < 4; cyl++) {
		auto pulses = rig.pulsesSince(cyl, from);
		ASSERT_GE(pulses.size(), 2u) << "injector " << cyl;

		for (const auto& pulse : pulses) {
			if (pulse.stillActive) {
				// Hasn't ended yet
				continue;
			}

			double endPhase = rig.sim->enginePhase(pulse.endAngle) - engine->cylinders[cyl].getAngleOffset();
			double late = std::remainder(endPhase, 720.0) + 180;
			EXPECT_GE(late, -1) << "injector " << cyl << " pulse at " << pulse.startUs;
			// A tooth is 12 degrees
			EXPECT_LE(late, 13) << "injector " << cyl << " pulse at " << pulse.startUs;
		}
	}
}

// The fast callback estimates each event's next pulse. It has to match the pulse that is then actually
// injected, wall wetting included, or the pulse would start late. Fuel is stepped up and down so that wall
// wetting is busy.
TEST(InjectionOutputs, DurationEstimateMatchesWhatIsInjected) {
	InjectionRig rig(IM_SEQUENTIAL);
	engineConfiguration->complexWallModel = false;
	engineConfiguration->wwaeTau = 0.3f;
	engineConfiguration->wwaeBeta = 0.4f;
	rig.run(5);

	// The estimate as it was at each tooth the decoder counted: an injection is committed on the last
	// tooth before it starts, with whatever the fast callback last worked out
	std::vector<std::pair<int64_t, float>> estimates;
	uint32_t lastRotation = getTriggerCentral()->getRotationCounter();
	rig.sim->onCrankEdge = [&](const SimSample& sample) {
		uint32_t rotation = getTriggerCentral()->getRotationCounter();
		if (rotation != lastRotation) {
			estimates.push_back({sample.timeUs, firstEvent().getDurationEstimate()});
		}
		lastRotation = rotation;
	};

	auto from = rig.sim->nowUs();
	rig.setPulseMs(12);
	rig.run(5);
	rig.setPulseMs(4);
	rig.run(5);

	auto pulses = rig.pulsesSince(0, from);
	ASSERT_GT(pulses.size(), 8u);

	bool wallWettingChangedSomething = false;
	for (const auto& pulse : pulses) {
		float estimate = NAN;
		for (const auto& [timeUs, value] : estimates) {
			if (timeUs <= pulse.startUs) {
				estimate = value;
			}
		}

		if (std::isnan(estimate)) {
			// Committed before recording started
			continue;
		}

		EXPECT_NEAR(estimate, pulse.durationMs(), 0.002f) << "pulse at " << pulse.startUs;

		if (std::abs(pulse.durationMs() - 12) > 0.1f && std::abs(pulse.durationMs() - 4) > 0.1f) {
			wallWettingChangedSomething = true;
		}
	}

	EXPECT_TRUE(wallWettingChangedSomething) << "wall wetting wasn't active, so this didn't test it";
}

// Pulses growing slowly until the start of injection moves back through the cylinder's own cycle start
// (end of injection 540 degrees in to its cycle, pulse longer than that): every intake still gets one pulse
TEST(InjectionOutputs, StartCrossingTheCycleStartKeepsOnePulsePerIntake) {
	InjectionRig rig(IM_SEQUENTIAL, trigger_type_e::TT_TOOTHED_WHEEL_60_2);
	rig.setPulseMs(20);
	rig.run(4);
	auto from = rig.sim->nowUs();

	// 20 ms is 360 degrees at this speed, 34 ms is 612: the start moves from 180 to -72
	for (float ms = 20; ms <= 34; ms += 0.5f) {
		rig.setPulseMs(ms);
		rig.run(1);
	}
	rig.run(3);

	EXPECT_TRUE(onePulsePerIntake(rig, from));
}

// The injection phase table crossing the 0/720 boundary between its cells while the engine is running:
// each injector still fires once per cycle, its timing moving smoothly. A table change between a pulse being
// committed and starting moves the following one, so allow a tooth (12 degrees) more than the change.
TEST(InjectionOutputs, PhaseTableCrossingZeroKeepsOnePulsePerCycle) {
	InjectionRig rig(IM_SEQUENTIAL, trigger_type_e::TT_TOOTHED_WHEEL_60_2);
	rig.setPulseMs(6);
	setTable(config->injectionPhase, -20.0f);
	rig.run(4);
	auto from = rig.sim->nowUs();

	// Ten steps of 4 degrees, through end of injection at TDC
	for (int step = 0; step < 10; step++) {
		setTable(config->injectionPhase, -20.0f + 4 * step);
		rig.run(1);
	}
	rig.run(2);

	EXPECT_TRUE(startSpacingsWithin(rig, from, 720 - 4 - 12, 720 + 4 + 12));
}

// Start of injection timing, with the table moving the start later by a lot at once: the start can land
// later in the same cycle as the pulse that just fired, but that cycle mustn't get a second pulse
TEST(InjectionOutputs, PhaseTableStepLaterNeverDoublesInStartTimingMode) {
	for (int toothOffset = 0; toothOffset < 60; toothOffset += 7) {
		InjectionRig rig(IM_SEQUENTIAL, trigger_type_e::TT_TOOTHED_WHEEL_60_2, [](EngineTestHelper&) {
			engineConfiguration->injectionTimingMode = InjectionTimingMode::Start;
		});
		rig.setPulseMs(6);
		rig.run(4);
		auto from = rig.sim->nowUs();

		rig.sim->runDegrees(toothOffset * 12);
		setTable(config->injectionPhase, -180.0f + 300);
		rig.run(6);

		EXPECT_TRUE(startSpacingsWithin(rig, from, 720 - 1, 720 + 300 + 1)) << "step at tooth " << toothOffset;
	}
}

// Fuel cut by the rev limiter and then allowed again: no burst of pulses on resume, and the first pulse
// ends where it should
TEST(InjectionOutputs, RevLimitCutAndResume) {
	InjectionRig rig(IM_SEQUENTIAL);
	rig.setPulseMs(6);
	rig.run(4);

	engineConfiguration->cutFuelOnHardLimit = true;
	engineConfiguration->rpmHardLimit = 2500;
	engineConfiguration->rpmHardLimitHyst = 100;
	rig.run(3);
	ASSERT_FALSE(getLimpManager()->allowInjection().value) << "the rev limiter didn't cut fuel";

	engineConfiguration->rpmHardLimit = 7000;
	auto from = rig.run(6);

	for (int cyl = 0; cyl < 4; cyl++) {
		auto pulses = rig.pulsesSince(cyl, from);
		ASSERT_GE(pulses.size(), 3u) << "injector " << cyl;

		for (const auto& pulse : pulses) {
			double endPhase = rig.sim->enginePhase(pulse.endAngle) - engine->cylinders[cyl].getAngleOffset();
			EXPECT_NEAR(-180, std::remainder(endPhase, 720.0), 1)
					<< "injector " << cyl << " pulse at " << pulse.startUs;
		}
	}

	EXPECT_TRUE(startSpacingsWithin(rig, from, 720 - 1, 720 + 1));
}

// Batch mode on a crank wheel, before the cam has been seen: which revolution is which is a guess. When
// the cam turns up and shows the guess was wrong, the engine phase is relabeled by 360 degrees. The
// injectors are fired in pairs a revolution apart, so the pairs trade places, and each injector must keep
// getting exactly one pulse per revolution, whatever the injection timing mode. The cam tooth is tried on
// each revolution, since only one of them makes the first guess wrong.
TEST(InjectionOutputs, BatchThroughCamSyncKeepsOnePulsePerRevolution) {
	int relabeled = 0;

	for (auto timingMode : {InjectionTimingMode::End, InjectionTimingMode::Center, InjectionTimingMode::Start}) {
		for (float camRevolution : {0.0f, 360.0f}) {
			InjectionRig rig(IM_BATCH, trigger_type_e::TT_ONE, [timingMode](EngineTestHelper& eth) {
				engineConfiguration->injectionTimingMode = timingMode;
				setCrankOperationMode();
				engineConfiguration->vvtMode[0] = VVT_SINGLE_TOOTH;
				engineConfiguration->camInputs[0] = Gpio::A10;
				engineConfiguration->trigger.customTotalToothCount = 36;
				engineConfiguration->trigger.customSkippedToothCount = 1;
				eth.setTriggerType(trigger_type_e::TT_TOOTHED_WHEEL);
			});
			rig.setPulseMs(3);
			rig.run(4);
			auto from = rig.sim->nowUs();
			auto relabelsBefore = getTriggerCentral()->phaseLabelJumpCount;

			rig.sim->addCam(0, {{(float)rig.sim->syncAngle() + 180 + camRevolution, 20}});
			rig.run(6);

			ASSERT_TRUE(getTriggerCentral()->triggerState.hasSynchronizedPhase());
			int relabels = getTriggerCentral()->phaseLabelJumpCount - relabelsBefore;
			relabeled += relabels;

			EXPECT_TRUE(startSpacingsWithin(rig, from, 360 - 1, 360 + 1))
					<< "timing mode " << (int)timingMode << ", cam on revolution " << camRevolution << ", " << relabels
					<< " relabels";
		}
	}

	// Otherwise this didn't test anything
	EXPECT_GE(relabeled, 3) << "the cam never corrected the phase";
}

// Duty cycle at and beyond 100%, where each pulse runs in to the next: the injector stays open, and once
// the fuel comes back down every injector is back to normal, with none left open
TEST(InjectionOutputs, DutyBeyond100PercentAndBack) {
	InjectionRig rig(IM_SEQUENTIAL, trigger_type_e::TT_TOOTHED_WHEEL_60_2);

	// Out of the way of the duty cycle protection, which would otherwise cut fuel
	engineConfiguration->maxInjectorDutyInstant = 200;
	engineConfiguration->maxInjectorDutySustained = 120;
	engineConfiguration->maxInjectorDutySustainedTimeout = 5;

	// Get there in small steps: big ones skip injections, which is tested elsewhere. One cycle is 40 ms.
	float ms = 3;
	for (float target : {34.0f, 40.0f, 50.0f, 60.0f}) {
		while (ms < target) {
			ms = std::min(target, ms + 1);
			rig.setPulseMs(ms);
			rig.run(1);
		}

		auto from = rig.run(6);
		auto settled = from + 3 * cycleUs;
		float expectedPerCycle = std::min(ms, 40.0f);

		EXPECT_TRUE(injectedPerWindowWithin(
				rig.recorder, settled, cycleUs, 3, 0.98f * expectedPerCycle, 1.02f * expectedPerCycle))
				<< ms << " ms pulses";
	}

	while (ms > 12) {
		ms = std::max(12.0f, ms - 1);
		rig.setPulseMs(ms);
		rig.run(1);
	}
	auto from = rig.run(6);
	EXPECT_TRUE(injectedPerWindowWithin(rig.recorder, from + 3 * cycleUs, cycleUs, 3, 0.99f * 12, 1.01f * 12));
	EXPECT_TRUE(noStuckInjector(rig.recorder, 13, from + 3 * cycleUs));
	stopAndCheckNothingLeftOpen(rig);
}

// Wall wetting changes every pulse for a while after a change in fuel. The estimate the pulses are placed
// by includes it, so once the change itself has gone through, none of those pulses starts late.
TEST(InjectionOutputs, WallWettingDoesNotMakePulsesLate) {
	InjectionRig rig(IM_SEQUENTIAL, trigger_type_e::TT_TOOTHED_WHEEL_60_2);
	engineConfiguration->complexWallModel = false;
	engineConfiguration->wwaeTau = 0.3f;
	engineConfiguration->wwaeBeta = 0.4f;
	rig.run(5);

	rig.setPulseMs(12);
	// The pulses that were due when the fuel changed may start late
	rig.run(2);

	auto lateBefore = engine->engineState.injectionLateCount;
	auto from = rig.run(8);

	EXPECT_EQ(lateBefore, engine->engineState.injectionLateCount);

	// And wall wetting really was changing the pulses meanwhile
	auto pulses = rig.pulsesSince(0, from);
	ASSERT_GE(pulses.size(), 4u);
	EXPECT_GT(std::abs(pulses.front().durationMs() - pulses.back().durationMs()), 0.1f);
}

// Starting the engine, on a crank wheel that needs the cam for engine phase: nothing has been injected
// yet, so nothing can be late, and no injection is counted as skipped for being too late
TEST(InjectionOutputs, EngineStartCountsNoSkips) {
	for (float camRevolution : {0.0f, 360.0f}) {
		InjectionRig rig(IM_SEQUENTIAL, trigger_type_e::TT_ONE, [](EngineTestHelper& eth) {
			setCrankOperationMode();
			engineConfiguration->vvtMode[0] = VVT_SINGLE_TOOTH;
			engineConfiguration->camInputs[0] = Gpio::A10;
			engineConfiguration->trigger.customTotalToothCount = 36;
			engineConfiguration->trigger.customSkippedToothCount = 1;
			eth.setTriggerType(trigger_type_e::TT_TOOTHED_WHEEL);
		});
		rig.sim->addCam(0, {{(float)rig.sim->syncAngle() + 180 + camRevolution, 20}});
		rig.run(6);

		ASSERT_TRUE(getTriggerCentral()->triggerState.hasSynchronizedPhase());
		EXPECT_EQ(0, engine->engineState.injectionSkippedLateCount) << "cam on revolution " << camRevolution;
		EXPECT_EQ(0, engine->engineState.injectionSkippedPoolCount) << "cam on revolution " << camRevolution;
	}
}

// Start of injection timing, with the table moving the start earlier by a lot at once. Each injector's
// next pulse starts earlier. If the new start is already behind the engine it starts late, as long as the
// pulse can still run (until a pulse length after its start), and otherwise that cycle's injection is lost,
// but then it's counted. It never fires twice in a cycle.
TEST(InjectionOutputs, PhaseTableStepEarlierInStartTimingModeCountsAnyLoss) {
	for (int toothOffset = 0; toothOffset < 60; toothOffset += 7) {
		InjectionRig rig(IM_SEQUENTIAL, trigger_type_e::TT_TOOTHED_WHEEL_60_2, [](EngineTestHelper&) {
			engineConfiguration->injectionTimingMode = InjectionTimingMode::Start;
		});
		rig.setPulseMs(6);
		// Include the last pulses before the step
		auto from = rig.run(4);
		auto skippedBefore = engine->engineState.injectionSkippedLateCount;

		rig.sim->runDegrees(toothOffset * 12);
		setTable(config->injectionPhase, -180.0f - 300);
		rig.run(6);

		int lost = 0;
		for (int cyl = 0; cyl < 4; cyl++) {
			for (double spacing : startSpacings(rig, cyl, from)) {
				// 300 earlier at the soonest, or a whole cycle and 300 earlier if that cycle was lost
				EXPECT_GE(spacing, 720 - 300 - 1) << "step at tooth " << toothOffset;
				EXPECT_LE(spacing, 2 * 720 - 300 + 1) << "step at tooth " << toothOffset;
				// More than the latest a late pulse could have started (a pulse is 108 degrees) means lost
				lost += spacing > 720 + 108 + 1;
			}
		}

		EXPECT_EQ(lost, engine->engineState.injectionSkippedLateCount - skippedBefore)
				<< "step at tooth " << toothOffset;
	}
}
