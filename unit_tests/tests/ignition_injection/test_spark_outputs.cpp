/**
 * @file test_spark_outputs.cpp
 *
 * What the coils really do, checked at the coil pins with a simulated engine (see
 * harness/engine_sim.h): where the sparks land, how long the coils charge, and that none go missing.
 */

#include "pch.h"

#include "harness/engine_invariants.h"

using ::testing::_;

extern WarningCodeState unitTestWarningCodeState;

namespace {

// Number of sparks a coil made that started at or after a point in time
size_t sparksSince(const OutputRecorder& recorder, int coil, int64_t fromUs) {
	size_t count = 0;

	for (const auto& pulse : recorder.pulses(OutputKind::Coil, coil)) {
		if (pulse.startUs >= fromUs) {
			count++;
		}
	}

	return count;
}

// A six cylinder with a distributor and a single coil, on a simple toothed cam wheel
void runDistributorEngine(int toothCount, float rpm) {
	EngineTestHelper eth(engine_type_e::FORD_INLINE_6_1995);
	EXPECT_CALL(*eth.mockAirmass, getAirmass(_, _)).WillRepeatedly(Return(AirmassResult{0.1008f, 50.0f}));

	// 13 degrees after TDC
	engineConfiguration->minimumIgnitionTiming = -15;
	setWholeTimingTable(-13);

	engineConfiguration->trigger.customTotalToothCount = toothCount;
	eth.applyTriggerWaveform();

	ASSERT_EQ(IM_ONE_COIL, engineConfiguration->ignitionMode);
	ASSERT_EQ(6u, engine->engineState.cylinderCount);

	OutputRecorder recorder;
	EngineSim sim(eth, &recorder);

	sim.setRpm(rpm);
	sim.runCycles(10);
	auto from = sim.nowUs();
	const int cycles = 10;
	sim.runCycles(cycles);

	EXPECT_TRUE(noTriggerErrors(sim));
	EXPECT_TRUE(staysSynced(sim, from));
	EXPECT_NEAR(rpm, Sensor::getOrZero(SensorType::Rpm), 1);
	EXPECT_TRUE(rpmWithin(sim, 0.001f, from));

	// The one coil fires for every cylinder, each spark at the commanded timing relative to the
	// TDC of whichever cylinder is next
	EXPECT_NEAR(6 * cycles, sparksSince(recorder, 0, from), 1);
	for (int coil = 1; coil < 6; coil++) {
		EXPECT_EQ(0u, sparksSince(recorder, coil, from)) << "coil " << coil;
	}

	EXPECT_TRUE(sparkTimingWithin(recorder, sim, -13, 0.2f, from, /*periodDegrees*/ 720.0 / 6));

	// Charged for as long as the firmware says it wants
	float dwellMs = engine->ignitionState.getDwell();
	EXPECT_GT(dwellMs, 0.1f);
	EXPECT_TRUE(dwellWithin(recorder, 0.98f * dwellMs, 1.02f * dwellMs, from));
	EXPECT_TRUE(noSparkWithoutCharge(recorder));

	// Fuel is steady too: every injector delivers the same amount every cycle
	int64_t cycleUs = 120e6 / rpm;
	float perCycle = activeTimePerWindow(recorder, OutputKind::Injector, 0, from, cycleUs, 1)[0];
	EXPECT_GT(perCycle, 1);
	EXPECT_TRUE(injectedPerWindowWithin(recorder, from, cycleUs, cycles, 0.99f * perCycle, 1.01f * perCycle));

	EXPECT_EQ(0, unitTestWarningCodeState.recentWarnings.getCount());
}

} // namespace

TEST(SparkOutputs, DistributorOnSixToothWheel) {
	runDistributorEngine(6, 2000);
}

TEST(SparkOutputs, DistributorOnEightToothWheel) {
	// Teeth that don't line up with the cylinders
	runDistributorEngine(8, 1500);
}

// Timing that moves, forwards and backwards across a trigger tooth, must not cost a spark.
// https://github.com/rusefi/rusefi/issues/299
TEST(SparkOutputs, TimingChangesMissNoSparks) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->ignitionMode = IM_WASTED_SPARK;
	// One tooth per engine cycle: its edges are at 0 and 360, right where the sparks are
	setupSimpleTestEngineWithMafAndTT_ONE_trigger(&eth);
	engineConfiguration->isIgnitionEnabled = true;
	engineConfiguration->isInjectionEnabled = false;

	OutputRecorder recorder;
	EngineSim sim(eth, &recorder);

	sim.setRpm(3000);
	sim.runCycles(10);

	const float dwellMs = engine->ignitionState.getDwell();
	auto from = sim.nowUs();
	int cycles = 0;

	// Degrees BTDC: either side of the tooth, then some big steps in both directions
	for (float timing : {0, 3, -5, 3, 20, -5, 40, 0}) {
		setWholeTimingTable(timing);

		// Give it a couple of cycles to get there...
		sim.runCycles(2);
		auto settled = sim.nowUs();
		sim.runCycles(2);
		cycles += 4;

		// ...then every spark is where it was asked for. Wasted spark: each coil fires every revolution.
		EXPECT_TRUE(sparkTimingWithin(recorder, sim, timing, 0.5f, settled, /*periodDegrees*/ 360))
				<< "timing " << timing;
	}

	// Through all of that, every coil fired exactly twice per cycle
	for (int coil = 0; coil < 4; coil++) {
		EXPECT_NEAR(2 * cycles, sparksSince(recorder, coil, from), 1) << "coil " << coil;
	}

	// While timing is on the move a coil may charge for a little less than intended (the spark is
	// held back until it has had 80%) or for longer (up to the overdwell limit of 1.5x, plus the time
	// it takes to notice), but never for long
	EXPECT_TRUE(dwellWithin(recorder, 0.75f * dwellMs, 1.8f * dwellMs, from));

	EXPECT_EQ(0, unitTestWarningCodeState.recentWarnings.getCount());
}

// Advance jumping by more than the dwell angle in one step, as when the idle timing controller
// switches from its lower to its upper limit on entering idle (seen at 1450 RPM: 12 to 35 degrees
// with about 16 degrees of dwell). The charge for the next spark still starts at the old dwell
// angle, but the new spark angle has already gone by, so that spark is fired by the overdwell
// backup instead. Its real fire must not stay queued behind it.
TEST(SparkOutputs, AdvanceStepLargerThanDwellAngle) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setCrankOperationMode();
	setupSimpleTestEngineWithMaf(&eth, IM_SEQUENTIAL, trigger_type_e::TT_TOOTHED_WHEEL_36_2);
	engineConfiguration->ignitionMode = IM_WASTED_SPARK;
	engineConfiguration->isIgnitionEnabled = true;
	engineConfiguration->isInjectionEnabled = false;
	setArrayValues(config->sparkDwellValues, 1.9f);

	OutputRecorder recorder;
	EngineSim sim(eth, &recorder);

	const float rpm = 1450;
	sim.setRpm(rpm);
	setWholeTimingTable(12);
	sim.runCycles(10);

	const float dwellMs = engine->ignitionState.getDwell();
	const float dwellAngle = dwellMs * rpm * 360 / 60e3;
	ASSERT_LT(dwellAngle, 35 - 12) << "the step has to be bigger than the dwell angle";

	ASSERT_NEAR(rpm, Sensor::getOrZero(SensorType::Rpm), 1);

	auto from = sim.nowUs();
	auto backupSparksBefore = engine->outputChannels.overdwellSparkCounter;

	setWholeTimingTable(35);
	sim.runCycles(2);
	auto settled = sim.nowUs();
	sim.runCycles(4);

	// This is the step that's too big: the backup fires at least one spark
	EXPECT_GT(engine->outputChannels.overdwellSparkCounter, backupSparksBefore);

	// Nothing re-added to the angle queue, and nothing fired twice
	EXPECT_EQ(nullptr, unitTestWarningCodeState.recentWarnings.find(ObdCode::CUSTOM_RE_ADDING_INTO_EXECUTION_QUEUE));
	for (int coil = 0; coil < 4; coil++) {
		EXPECT_NEAR(2 * 6, sparksSince(recorder, coil, from), 1) << "coil " << coil;
	}

	EXPECT_TRUE(noTriggerErrors(sim));
	EXPECT_TRUE(staysSynced(sim, from));

	// Once the step has gone through, every spark is where it was asked for
	EXPECT_TRUE(sparkTimingWithin(recorder, sim, 35, 0.5f, settled, /*periodDegrees*/ 360));
	EXPECT_TRUE(noSparkWithoutCharge(recorder));
	EXPECT_TRUE(dwellWithin(recorder, 0.75f * dwellMs, 1.8f * dwellMs, from));
}
