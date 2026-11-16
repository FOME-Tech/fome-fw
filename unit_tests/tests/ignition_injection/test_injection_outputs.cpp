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
	explicit InjectionRig(injection_mode_e mode) {
		EXPECT_CALL(*eth.mockAirmass, getAirmass(_, _)).WillRepeatedly([this](float, bool) {
			return AirmassResult{airmass, 50.0f};
		});

		// End of injection half a revolution before each cylinder's own TDC
		setTable(config->injectionPhase, -180.0f);
		setupSimpleTestEngineWithMafAndTT_ONE_trigger(&eth, mode);

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
	float airmass = 0.1008f;
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

// trigger_review I5: an injection is scheduled from the last tooth before it starts. This trigger has
// a tooth edge only every 360 degrees, and at high duty the gap between one pulse ending and the next
// starting is shorter than that. When the pulse gets longer its start moves earlier, in to a tooth's
// window that has already been handled, and that cylinder's next injection is skipped entirely.
TEST(InjectionOutputs, DISABLED_SequentialFuelFollowsPulseWidthChangesAtHighDuty) {
	InjectionRig rig(IM_SEQUENTIAL);

	stepFuelThrough(rig, {6, 12, 25, 35, 25, 12});

	stopAndCheckNothingLeftOpen(rig);
}

// trigger_review I5: the same thing in batch mode takes a bigger step. This is the 1.5 ms to 12.5 ms
// pulse width change of the original issue 299 tests: one pair of cylinders gets a cycle about half
// short of fuel.
TEST(InjectionOutputs, DISABLED_BatchFuelFollowsLargePulseWidthStep) {
	InjectionRig rig(IM_BATCH);

	stepFuelThrough(rig, {25});

	stopAndCheckNothingLeftOpen(rig);
}
