/*
 * @file test_rpm_multiplier.cpp
 *
 * The same engine speed must read as the same RPM, whether the trigger wheel turns at
 * crank speed or cam speed, and whether the engine is two or four stroke.
 *
 * @date Jun 26, 2021
 * @author Andrey Belomutskiy, (c) 2012-2021
 */

#include "pch.h"

#include "harness/engine_invariants.h"

static void runRpmTest(bool isTwoStroke, bool isCam, float expectedWheelPeriod) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->twoStroke = isTwoStroke;
	engineConfiguration->skippedWheelOnCam = isCam;
	eth.setTriggerType(trigger_type_e::TT_ONE);

	EngineSim sim(eth);
	EXPECT_EQ(expectedWheelPeriod, sim.wheelPeriod());

	sim.setRpm(1500);
	sim.runCycles(20);
	auto from = sim.nowUs();
	sim.runCycles(20);

	EXPECT_NEAR(1500, Sensor::getOrZero(SensorType::Rpm), 1);
	EXPECT_TRUE(rpmWithin(sim, 0.001f, from));
}

TEST(engine, testRpmOfCamSensor) {
	// One tooth per two crank revolutions
	runRpmTest(false, true, 720);
}

TEST(engine, testRpmOfTwoStroke) {
	runRpmTest(true, false, 360);
}

TEST(engine, testRpmOfCrankOnly) {
	// One tooth per crank revolution
	runRpmTest(false, false, 360);
}
