#include "pch.h"

float GetGearRatioFor(float revPerKm, float axle, float kph, float rpm) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	engineConfiguration->driveWheelRevPerKm = revPerKm;
	engineConfiguration->finalGearRatio = axle;

	Sensor::setMockValue(SensorType::VehicleSpeed, kph);
	Sensor::setMockValue(SensorType::Rpm, rpm);

	engine->periodicSlowCallback();

	auto& dut = engine->module<GearDetector>().unmock();
	return dut.getGearboxRatio();
}

TEST(GearDetector, ComputeGearRatio) {
	// real gears from Volvo racecar
	EXPECT_NEAR_M3(3.35f, GetGearRatioFor(507, 4.1, 29.45f / 0.6214f, 5500));
	EXPECT_NEAR_M3(1.99f, GetGearRatioFor(507, 4.1, 49.57f / 0.6214f, 5500));
	EXPECT_NEAR_M3(1.33f, GetGearRatioFor(507, 4.1, 74.18f / 0.6214f, 5500));
	EXPECT_NEAR_M3(1.00f, GetGearRatioFor(507, 4.1, 98.65f / 0.6214f, 5500));
	EXPECT_NEAR_M3(0.72f, GetGearRatioFor(507, 4.1, 137.02f / 0.6214f, 5500));

	// Idling, car stopped, check no div/0
	EXPECT_EQ(0, GetGearRatioFor(507, 4.1, 0, 800));
}

TEST(GearDetector, GetRpmInGear) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	engineConfiguration->driveWheelRevPerKm = 507;
	engineConfiguration->finalGearRatio = 4.10f;

	// real gears from Volvo racecar
	engineConfiguration->totalGearsCount = 5;
	engineConfiguration->gearRatio[0] = 3.35f;
	engineConfiguration->gearRatio[1] = 1.99f;
	engineConfiguration->gearRatio[2] = 1.33f;
	engineConfiguration->gearRatio[3] = 1.00f;
	engineConfiguration->gearRatio[4] = 0.72f;

	auto& dut = engine->module<GearDetector>().unmock();

	Sensor::setMockValue(SensorType::VehicleSpeed, 29.45f / 0.6214f);
	EXPECT_NEAR(5500, dut.getRpmInGear(1), 1);
	Sensor::setMockValue(SensorType::VehicleSpeed, 49.57f / 0.6214f);
	EXPECT_NEAR(5500, dut.getRpmInGear(2), 1);
	Sensor::setMockValue(SensorType::VehicleSpeed, 74.18f / 0.6214f);
	EXPECT_NEAR(5500, dut.getRpmInGear(3), 1);
	Sensor::setMockValue(SensorType::VehicleSpeed, 98.65f / 0.6214f);
	EXPECT_NEAR(5500, dut.getRpmInGear(4), 1);
	Sensor::setMockValue(SensorType::VehicleSpeed, 137.02f / 0.6214f);
	EXPECT_NEAR(5500, dut.getRpmInGear(5), 1);

	// Test some invalid cases
	EXPECT_FLOAT_EQ(0, dut.getRpmInGear(0));
	EXPECT_FLOAT_EQ(0, dut.getRpmInGear(10));

	// Zero vehicle speed shouldn't cause a problem
	Sensor::setMockValue(SensorType::VehicleSpeed, 0);
	EXPECT_FLOAT_EQ(0, dut.getRpmInGear(0));
	EXPECT_FLOAT_EQ(0, dut.getRpmInGear(1));
	EXPECT_FLOAT_EQ(0, dut.getRpmInGear(5));
	EXPECT_FLOAT_EQ(0, dut.getRpmInGear(10));
}

TEST(GearDetector, DetermineGearSingleSpeed) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& dut = engine->module<GearDetector>().unmock();

	engineConfiguration->totalGearsCount = 1;
	engineConfiguration->gearRatio[0] = 2;

	dut.onConfigurationChange(nullptr);

	// Super high ratios indicate clutch slip or idling in neutral or something
	EXPECT_EQ(0, dut.determineGearFromRatio(100));
	EXPECT_EQ(0, dut.determineGearFromRatio(4));

	// Check exactly on the gear
	EXPECT_EQ(1, dut.determineGearFromRatio(2));

	// Check near the gear
	EXPECT_EQ(1, dut.determineGearFromRatio(2.1));
	EXPECT_EQ(1, dut.determineGearFromRatio(1.9));

	// Extremely low ratio suggests stopped engine at speed?
	EXPECT_EQ(0, dut.determineGearFromRatio(1.0));
}

TEST(GearDetector, DetermineGear5Speed) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& dut = engine->module<GearDetector>().unmock();

	engineConfiguration->totalGearsCount = 5;
	engineConfiguration->gearRatio[0] = 3.35;
	engineConfiguration->gearRatio[1] = 1.99;
	engineConfiguration->gearRatio[2] = 1.33;
	engineConfiguration->gearRatio[3] = 1.00;
	engineConfiguration->gearRatio[4] = 0.72;

	dut.onConfigurationChange(nullptr);

	// Super high ratios indicate clutch slip or idling in neutral or something
	EXPECT_EQ(0, dut.determineGearFromRatio(100));
	EXPECT_EQ(0, dut.determineGearFromRatio(6));

	// Check exactly on gears
	EXPECT_EQ(1, dut.determineGearFromRatio(3.35));
	EXPECT_EQ(2, dut.determineGearFromRatio(1.99));
	EXPECT_EQ(3, dut.determineGearFromRatio(1.33));
	EXPECT_EQ(4, dut.determineGearFromRatio(1.00));
	EXPECT_EQ(5, dut.determineGearFromRatio(0.72));

	// Check near each gear
	EXPECT_EQ(1, dut.determineGearFromRatio(3.45));
	EXPECT_EQ(1, dut.determineGearFromRatio(3.25));

	EXPECT_EQ(2, dut.determineGearFromRatio(2.2));
	EXPECT_EQ(2, dut.determineGearFromRatio(1.8));

	EXPECT_EQ(3, dut.determineGearFromRatio(1.45));
	EXPECT_EQ(3, dut.determineGearFromRatio(1.25));

	EXPECT_EQ(4, dut.determineGearFromRatio(1.1));
	EXPECT_EQ(4, dut.determineGearFromRatio(0.9));

	EXPECT_EQ(5, dut.determineGearFromRatio(0.8));
	EXPECT_EQ(5, dut.determineGearFromRatio(0.6));

	// Extremely low ratio suggests stopped engine at speed?
	EXPECT_EQ(0, dut.determineGearFromRatio(0.1));
}

TEST(GearDetector, MiataNb6Speed) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& dut = engine->module<GearDetector>().unmock();

	engineConfiguration->totalGearsCount = 6;
	engineConfiguration->gearRatio[0] = 3.76;
	engineConfiguration->gearRatio[1] = 2.27;
	engineConfiguration->gearRatio[2] = 1.65;
	engineConfiguration->gearRatio[3] = 1.26;
	engineConfiguration->gearRatio[4] = 1.00;
	engineConfiguration->gearRatio[5] = 0.84;
	engineConfiguration->gearRatio[6] = 0.84;
	engineConfiguration->gearRatio[7] = 0.84;

	dut.onConfigurationChange(nullptr);

	EXPECT_EQ(0, dut.determineGearFromRatio(5.85));
	EXPECT_EQ(1, dut.determineGearFromRatio(5.51));

	// Check exactly on gears
	EXPECT_EQ(1, dut.determineGearFromRatio(3.76));
	EXPECT_EQ(2, dut.determineGearFromRatio(2.27));
	EXPECT_EQ(3, dut.determineGearFromRatio(1.65));
	EXPECT_EQ(4, dut.determineGearFromRatio(1.26));
	EXPECT_EQ(5, dut.determineGearFromRatio(1.00));
	EXPECT_EQ(6, dut.determineGearFromRatio(0.84));
}

TEST(GearDetector, DetermineGear8Speed) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& dut = engine->module<GearDetector>().unmock();

	// ZF 8HP 70
	engineConfiguration->totalGearsCount = 8;
	engineConfiguration->gearRatio[0] = 4.69;
	engineConfiguration->gearRatio[1] = 3.13;
	engineConfiguration->gearRatio[2] = 2.10;
	engineConfiguration->gearRatio[3] = 1.67;
	engineConfiguration->gearRatio[4] = 1.28;
	engineConfiguration->gearRatio[5] = 1;
	engineConfiguration->gearRatio[6] = 0.84;
	engineConfiguration->gearRatio[7] = 0.67;

	dut.onConfigurationChange(nullptr);

	// Super high ratios indicate clutch slip or idling in neutral or something
	EXPECT_EQ(0, dut.determineGearFromRatio(100));
	EXPECT_EQ(0, dut.determineGearFromRatio(8));

	// Check exactly on gears - only test the ends, the middle works
	EXPECT_EQ(1, dut.determineGearFromRatio(4.69));
	EXPECT_EQ(2, dut.determineGearFromRatio(3.13));

	EXPECT_EQ(7, dut.determineGearFromRatio(0.84));
	EXPECT_EQ(8, dut.determineGearFromRatio(0.67));

	// Check near each gear - only test the ends, the middle works
	EXPECT_EQ(1, dut.determineGearFromRatio(4.75));
	EXPECT_EQ(1, dut.determineGearFromRatio(4.3));

	EXPECT_EQ(8, dut.determineGearFromRatio(0.71));
	EXPECT_EQ(8, dut.determineGearFromRatio(0.6));

	// Extremely low ratio suggests stopped engine at speed?
	EXPECT_EQ(0, dut.determineGearFromRatio(0.1));
}

TEST(GearDetector, TotalRatioInCurrentGear) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& dut = engine->module<GearDetector>().unmock();

	engineConfiguration->driveWheelRevPerKm = 600;
	engineConfiguration->finalGearRatio = 4.0f;
	engineConfiguration->totalGearsCount = 2;
	engineConfiguration->gearRatio[0] = 3.0f;
	engineConfiguration->gearRatio[1] = 1.5f;
	dut.onConfigurationChange(nullptr);

	// Let a gear settle in: the detector wants to see a ratio hold still before it believes it
	auto settle = [&eth, &dut]() {
		for (int i = 0; i < 5; i++) {
			eth.moveTimeForwardMs(SLOW_CALLBACK_PERIOD_MS);
			dut.onSlowCallback();
		}
	};

	// No ratio while stopped / in neutral.
	Sensor::setMockValue(SensorType::VehicleSpeed, 0);
	Sensor::setMockValue(SensorType::Rpm, 800);
	settle();
	EXPECT_FALSE(dut.getTotalRatioInCurrentGear().Valid);

	// wheelRpm = 50 * 600 / 60 = 500; driveshaftRpm = 500 * 4 = 2000.
	Sensor::setMockValue(SensorType::VehicleSpeed, 50);

	// gearboxRatio = engineRpm / 2000 = 3.0 -> 1st gear; total ratio = 3.0 * 4.0 = 12.
	Sensor::setMockValue(SensorType::Rpm, 6000);
	settle();
	ASSERT_TRUE(dut.getTotalRatioInCurrentGear().Valid);
	EXPECT_NEAR(dut.getTotalRatioInCurrentGear().Value, 12.0f, 1e-3);

	// gearboxRatio = 3000 / 2000 = 1.5 -> 2nd gear; total ratio = 1.5 * 4.0 = 6.
	Sensor::setMockValue(SensorType::Rpm, 3000);
	settle();
	ASSERT_TRUE(dut.getTotalRatioInCurrentGear().Valid);
	EXPECT_NEAR(dut.getTotalRatioInCurrentGear().Value, 6.0f, 1e-3);
}

TEST(GearDetector, ParameterValidation) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& dut = engine->module<GearDetector>().unmock();

	// Defaults should work
	EXPECT_NO_FATAL_ERROR(dut.onConfigurationChange(nullptr));

	// Invalid gear count
	engineConfiguration->totalGearsCount = 25;
	EXPECT_FATAL_ERROR(dut.onConfigurationChange(nullptr));

	// Valid gears
	engineConfiguration->totalGearsCount = 2;
	engineConfiguration->gearRatio[0] = 3;
	engineConfiguration->gearRatio[1] = 2;
	EXPECT_NO_FATAL_ERROR(dut.onConfigurationChange(nullptr));

	// Invalid gear ratio
	engineConfiguration->gearRatio[1] = 0;
	EXPECT_FATAL_ERROR(dut.onConfigurationChange(nullptr));

	// Out of order gear ratios
	engineConfiguration->gearRatio[0] = 2;
	engineConfiguration->gearRatio[1] = 3;
	EXPECT_FATAL_ERROR(dut.onConfigurationChange(nullptr));

	// No gears at all is a valid configuration
	engineConfiguration->totalGearsCount = 0;
	EXPECT_NO_FATAL_ERROR(dut.onConfigurationChange(nullptr));
}

TEST(GearDetector, HysteresisNearThreshold) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& dut = engine->module<GearDetector>().unmock();

	// real gears from Volvo racecar
	engineConfiguration->totalGearsCount = 5;
	engineConfiguration->gearRatio[0] = 3.35;
	engineConfiguration->gearRatio[1] = 1.99;
	engineConfiguration->gearRatio[2] = 1.33;
	engineConfiguration->gearRatio[3] = 1.00;
	engineConfiguration->gearRatio[4] = 0.72;

	dut.onConfigurationChange(nullptr);

	// The 3rd/4th boundary sits at sqrt(1.33 * 1.00) = 1.153
	EXPECT_EQ(3, dut.determineGearFromRatio(1.17, 0));
	EXPECT_EQ(4, dut.determineGearFromRatio(1.13, 0));

	// Holding a gear drags the boundary along with it: a ratio that just barely crossed doesn't
	// pull us out of the gear we're in
	EXPECT_EQ(4, dut.determineGearFromRatio(1.17, 4));
	EXPECT_EQ(3, dut.determineGearFromRatio(1.13, 3));

	// ...but a ratio clearly on the other side does
	EXPECT_EQ(3, dut.determineGearFromRatio(1.25, 4));
	EXPECT_EQ(4, dut.determineGearFromRatio(1.05, 3));

	// Holding a gear doesn't bias boundaries it isn't adjacent to
	EXPECT_EQ(3, dut.determineGearFromRatio(1.17, 1));
	EXPECT_EQ(4, dut.determineGearFromRatio(1.13, 1));
}

// Drives the gear detector one slow callback at a time. Vehicle speed is fixed at 100 kph, which
// with these wheel/axle settings is exactly 1000 rpm at the driveshaft - so engine rpm is just the
// gearbox ratio times 1000, and tests can talk in ratios.
class GearDetectorTester {
public:
	GearDetectorTester() {
		engineConfiguration->driveWheelRevPerKm = 600;
		engineConfiguration->finalGearRatio = 1;

		// real gears from Volvo racecar
		engineConfiguration->totalGearsCount = 5;
		engineConfiguration->gearRatio[0] = 3.35;
		engineConfiguration->gearRatio[1] = 1.99;
		engineConfiguration->gearRatio[2] = 1.33;
		engineConfiguration->gearRatio[3] = 1.00;
		engineConfiguration->gearRatio[4] = 0.72;

		dut().onConfigurationChange(nullptr);

		Sensor::setMockValue(SensorType::VehicleSpeed, 100);
	}

	GearDetector& dut() {
		return engine->module<GearDetector>().unmock();
	}

	// Run one slow callback with the driveline turning at the given gearbox ratio
	size_t tick(float ratio) {
		Sensor::setMockValue(SensorType::Rpm, 1000 * ratio);
		return tickCommon();
	}

	// Run one slow callback with the vehicle stopped (or the VSS dropped out)
	size_t tickNoVss() {
		Sensor::setMockValue(SensorType::VehicleSpeed, 0);
		size_t gear = tickCommon();
		Sensor::setMockValue(SensorType::VehicleSpeed, 100);
		return gear;
	}

	size_t tickFor(float ratio, int count) {
		size_t gear = 0;
		for (int i = 0; i < count; i++) {
			gear = tick(ratio);
		}

		return gear;
	}

	EngineTestHelper eth{engine_type_e::TEST_ENGINE};

private:
	size_t tickCommon() {
		eth.moveTimeForwardMs(SLOW_CALLBACK_PERIOD_MS);
		engine->periodicSlowCallback();
		return dut().get().Value;
	}
};

TEST(GearDetector, SettlesIntoGear) {
	GearDetectorTester dut;

	// A gear we're sitting exactly on still has to hold still for a moment before we believe it
	EXPECT_EQ(0, dut.tick(1.00));
	EXPECT_EQ(0, dut.tick(1.00));

	// ...but "a moment" is short: a few slow callbacks, not the better part of a second
	EXPECT_EQ(4, dut.tickFor(1.00, 2));
}

TEST(GearDetector, SnappyOnCleanShift) {
	GearDetectorTester dut;

	ASSERT_EQ(4, dut.tickFor(1.00, 6));

	// Clutch dumped straight into 3rd: the ratio steps to exactly 3rd and stays there
	EXPECT_EQ(4, dut.tickFor(1.33, 2));
	EXPECT_EQ(3, dut.tickFor(1.33, 2));
}

TEST(GearDetector, RejectsOverBlippedDownshift) {
	GearDetectorTester dut;

	ASSERT_EQ(4, dut.tickFor(1.00, 6));

	// 4-3 downshift with the blip overshooting all the way past 2nd gear's ratio before falling
	// back onto 3rd. The old detector would report 4-3-2-3.
	const float blip[] = {1.05, 1.20, 1.45, 1.75, 1.99, 1.92, 1.70, 1.45, 1.33};

	for (float ratio : blip) {
		// We may not know what gear we're in mid-shift, but we must never claim 2nd: the ratio only
		// swept through it, it never sat there.
		EXPECT_NE(2, dut.tick(ratio)) << "ratio " << ratio;
	}

	// Once the ratio settles on 3rd we pick it up promptly
	EXPECT_EQ(3, dut.tickFor(1.33, 3));
}

TEST(GearDetector, RejectsUndershotUpshift) {
	GearDetectorTester dut;

	ASSERT_EQ(3, dut.tickFor(1.33, 6));

	// 3-4 upshift where the revs fall past 4th, down through 5th's ratio, then recover onto 4th
	const float undershoot[] = {1.25, 1.10, 0.95, 0.80, 0.72, 0.78, 0.90, 1.00};

	for (float ratio : undershoot) {
		EXPECT_NE(5, dut.tick(ratio)) << "ratio " << ratio;
	}

	EXPECT_EQ(4, dut.tickFor(1.00, 3));
}

TEST(GearDetector, HoldsGearThroughVssDropout) {
	GearDetectorTester dut;

	ASSERT_EQ(3, dut.tickFor(1.33, 6));

	// A dropped VSS frame or two shouldn't drop us out of gear
	EXPECT_EQ(3, dut.tickNoVss());
	EXPECT_EQ(3, dut.tickNoVss());
	EXPECT_EQ(3, dut.tick(1.33));

	// ...but genuinely losing vehicle speed for a while does
	for (int i = 0; i < 10; i++) {
		dut.tickNoVss();
	}

	EXPECT_EQ(0, dut.tickNoVss());
}

TEST(GearDetector, StaysInGearNearThreshold) {
	GearDetectorTester dut;

	ASSERT_EQ(4, dut.tickFor(1.00, 6));

	// Slightly wrong tire size or gear ratio config can park the measured ratio right on a
	// boundary - we should pick a gear and stay there rather than chattering
	for (int i = 0; i < 20; i++) {
		EXPECT_EQ(4, dut.tick(i % 2 ? 1.14 : 1.16)) << "iteration " << i;
	}
}
