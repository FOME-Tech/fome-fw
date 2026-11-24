/*
 * @file test_multispark.cpp
 *
 * @date Mar 15, 2020
 * @author Matthew Kennedy, (c) 2020
 */

#include "pch.h"

#include "harness/engine_invariants.h"
#include "spark_logic.h"

TEST(Multispark, DefaultConfiguration) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	EXPECT_EQ(0, getMultiSparkCount(0));
	EXPECT_EQ(0, getMultiSparkCount(100));
	EXPECT_EQ(0, getMultiSparkCount(200));
	EXPECT_EQ(0, getMultiSparkCount(500));
	EXPECT_EQ(0, getMultiSparkCount(1000));
	EXPECT_EQ(0, getMultiSparkCount(2000));
	EXPECT_EQ(0, getMultiSparkCount(5000));
	EXPECT_EQ(0, getMultiSparkCount(50000));
}

static void multisparkCfg() {
	// Turn it on!
	engineConfiguration->multisparkEnable = true;

	// Fire up to 45 degrees worth of sparks...
	engineConfiguration->multisparkMaxSparkingAngle = 45;

	// ...but limit to 10 additional sparks
	engineConfiguration->multisparkMaxExtraSparkCount = 10;

	// 3ms period (spark + dwell)
	engineConfiguration->multisparkDwell = 2;
	engineConfiguration->multisparkSparkDuration = 1;
}

TEST(Multispark, EnabledNoMaxRpm) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	multisparkCfg();

	// Practically no RPM limit
	engineConfiguration->multisparkMaxRpm = 12500;

	EXPECT_EQ(0, getMultiSparkCount(0));
	EXPECT_EQ(10, getMultiSparkCount(150));
	EXPECT_EQ(10, getMultiSparkCount(250));
	EXPECT_EQ(4, getMultiSparkCount(550));
	EXPECT_EQ(3, getMultiSparkCount(800));
	EXPECT_EQ(2, getMultiSparkCount(900));
	EXPECT_EQ(1, getMultiSparkCount(1500));

	// 2500 is the threshold where we should get zero
	EXPECT_EQ(1, getMultiSparkCount(2499));
	EXPECT_EQ(0, getMultiSparkCount(2501));

	EXPECT_EQ(0, getMultiSparkCount(5000));

	EXPECT_EQ(0, getMultiSparkCount(50000));
}

TEST(Multispark, RpmLimit) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	multisparkCfg();

	// Disable at 800 rpm
	engineConfiguration->multisparkMaxRpm = 800;

	EXPECT_EQ(3, getMultiSparkCount(795));
	EXPECT_EQ(0, getMultiSparkCount(805));
}

namespace {

// Wasted spark four cylinder on a 36-2 wheel with multispark: 3ms main dwell, then extra sparks
// with a much shorter 1ms dwell, 0.5ms apart
void setupMultisparkEngine(EngineTestHelper& eth) {
	setCrankOperationMode();
	setupSimpleTestEngineWithMaf(&eth, IM_SEQUENTIAL, trigger_type_e::TT_TOOTHED_WHEEL_36_2);
	engineConfiguration->ignitionMode = IM_WASTED_SPARK;
	engineConfiguration->isIgnitionEnabled = true;
	engineConfiguration->isInjectionEnabled = false;
	setArrayValues(config->sparkDwellValues, 3.0f);
	setWholeTimingTable(10);

	engineConfiguration->multisparkEnable = true;
	engineConfiguration->multisparkMaxRpm = 3000;
	engineConfiguration->multisparkMaxSparkingAngle = 45;
	engineConfiguration->multisparkMaxExtraSparkCount = 3;
	engineConfiguration->multisparkDwell = 1;
	engineConfiguration->multisparkSparkDuration = 0.5f;
}

} // namespace

// Every extra spark fires, each charged for the multispark dwell, not held back to 80% of the
// (much longer) main spark's dwell
TEST(Multispark, AllExtraSparksFireWithTheirOwnDwell) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setupMultisparkEngine(eth);

	OutputRecorder recorder;
	EngineSim sim(eth, &recorder);

	// 45 degrees at 1000 rpm is 7.5ms, room for 5 extra sparks of 1.5ms, limited to 3
	sim.setRpm(1000);
	sim.runCycles(10);
	ASSERT_NEAR(1000, Sensor::getOrZero(SensorType::Rpm), 1);
	ASSERT_EQ(3, engine->engineState.multispark.count);

	const float mainDwellMs = engine->ignitionState.getDwell();
	ASSERT_NEAR(3, mainDwellMs, 0.01f);

	auto from = sim.nowUs();
	sim.runCycles(10);
	// Let the last spark train finish
	sim.runDegrees(90);

	size_t mainSparks = 0;
	size_t extraSparks = 0;

	for (int coil = 0; coil < 4; coil++) {
		bool seenMain = false;

		for (const auto& pulse : recorder.pulses(OutputKind::Coil, coil)) {
			if (pulse.startUs < from || pulse.stillActive) {
				continue;
			}

			if (pulse.durationMs() > 2) {
				seenMain = true;
				mainSparks++;
				EXPECT_NEAR(mainDwellMs, pulse.durationMs(), 0.05f * mainDwellMs);
			} else if (seenMain) {
				// Only count the extra sparks that follow a main spark counted above
				extraSparks++;
				EXPECT_NEAR(1, pulse.durationMs(), 0.05f) << "coil " << coil << " extra spark at " << pulse.startUs;
			}
		}
	}

	// Wasted spark: each of the four coils fires every revolution
	EXPECT_NEAR(4 * 2 * 10, mainSparks, 2);
	EXPECT_EQ(3 * mainSparks, extraSparks);
}

// An extra spark that really is short of its (multispark) dwell is held back until it has had 80%
// of it, and the rest of the train is dropped. The main spark is still held to 80% of the main dwell.
TEST(Multispark, UnderdwelledSparkIsHeldBack) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setupMultisparkEngine(eth);

	engine->engineState.multispark.delay = efidur_t{(uint32_t)USF2NT(500)};
	engine->engineState.multispark.dwell = efidur_t{(uint32_t)USF2NT(1000)};
	engine->engineState.multispark.dwellMs = 1;

	std::vector<std::pair<int64_t, bool>> edges;
	engine->onIgnitionEvent = [&](const IgnitionContext&, bool charge) { edges.emplace_back(getTimeNowUs(), charge); };

	auto runOne = [&](bool isExtraSpark) {
		edges.clear();

		// The main spark's dwell (firing a spark re-plans this from the engine state)
		engine->ignitionEvents.elements[0].sparkDwell = 3;

		IgnitionContext ctx;
		ctx.outputsMask = 1;
		ctx.eventIndex = 0;
		ctx.sparksRemaining = 2;
		ctx.isExtraSpark = isExtraSpark;

		turnSparkPinHigh(ctx);
		auto chargeUs = getTimeNowUs();

		// The firing comes far too early: only 0.2ms of charge
		eth.moveTimeForwardUs(200);
		fireSparkAndPrepareNextSchedule(ctx);
		EXPECT_TRUE(enginePins.coils[0].getLogicValue()) << "fired underdwelled";

		eth.moveTimeForwardAndInvokeEventsUs(20000);

		// Charged once, fired once, and no more sparks after it
		EXPECT_EQ(2u, edges.size());
		EXPECT_FALSE(enginePins.coils[0].getLogicValue());
		if (edges.size() >= 2) {
			EXPECT_TRUE(edges[0].second);
			EXPECT_FALSE(edges[1].second);
			return edges[1].first - chargeUs;
		}

		return int64_t{0};
	};

	// 80% of the 1ms multispark dwell
	EXPECT_NEAR(800, runOne(true), 10);
	// 80% of the 3ms main dwell
	EXPECT_NEAR(2400, runOne(false), 10);
}
