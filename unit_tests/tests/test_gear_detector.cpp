#include "pch.h"

#include <set>

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

	// No ratio while stopped / in neutral.
	Sensor::setMockValue(SensorType::VehicleSpeed, 0);
	Sensor::setMockValue(SensorType::Rpm, 800);
	dut.onSlowCallback();
	EXPECT_FALSE(dut.getTotalRatioInCurrentGear().Valid);

	// wheelRpm = 50 * 600 / 60 = 500; driveshaftRpm = 500 * 4 = 2000.
	Sensor::setMockValue(SensorType::VehicleSpeed, 50);

	// gearboxRatio = engineRpm / 2000 = 3.0 -> 1st gear; total ratio = 3.0 * 4.0 = 12.
	Sensor::setMockValue(SensorType::Rpm, 6000);
	dut.onSlowCallback();
	ASSERT_TRUE(dut.getTotalRatioInCurrentGear().Valid);
	EXPECT_NEAR(dut.getTotalRatioInCurrentGear().Value, 12.0f, 1e-3);

	// gearboxRatio = 3000 / 2000 = 1.5 -> 2nd gear; total ratio = 1.5 * 4.0 = 6.
	Sensor::setMockValue(SensorType::Rpm, 3000);
	dut.onSlowCallback();
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

TEST(GearDetector, DecideClutchPosition) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	engineConfiguration->clutchAppliedBelow = 15;
	engineConfiguration->clutchReleasedAbove = 60;

	Sensor::setInvalidMockValue(SensorType::ClutchPosition);
	EXPECT_EQ(ClutchState::Indeterminate, decideClutchPosition());

	Sensor::setMockValue(SensorType::ClutchPosition, 0);
	EXPECT_EQ(ClutchState::Engaged, decideClutchPosition());
	Sensor::setMockValue(SensorType::ClutchPosition, 14.9f);
	EXPECT_EQ(ClutchState::Engaged, decideClutchPosition());
	Sensor::setMockValue(SensorType::ClutchPosition, 15);
	EXPECT_EQ(ClutchState::Middle, decideClutchPosition());
	Sensor::setMockValue(SensorType::ClutchPosition, 60);
	EXPECT_EQ(ClutchState::Middle, decideClutchPosition());
	Sensor::setMockValue(SensorType::ClutchPosition, 60.1f);
	EXPECT_EQ(ClutchState::Disengaged, decideClutchPosition());
	Sensor::setMockValue(SensorType::ClutchPosition, 100);
	EXPECT_EQ(ClutchState::Disengaged, decideClutchPosition());

	// Thresholds the wrong way round are unusable
	engineConfiguration->clutchAppliedBelow = 60;
	engineConfiguration->clutchReleasedAbove = 15;
	EXPECT_EQ(ClutchState::Indeterminate, decideClutchPosition());
}

TEST(GearDetector, LockedGearTolerance) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& dut = engine->module<GearDetector>().unmock();

	// Widely spaced gears: 4% tolerance
	engineConfiguration->totalGearsCount = 2;
	engineConfiguration->gearRatio[0] = 3.0f;
	engineConfiguration->gearRatio[1] = 2.0f;
	dut.onConfigurationChange(nullptr);

	EXPECT_EQ(1, dut.determineLockedGear(3.0f));
	EXPECT_EQ(1, dut.determineLockedGear(3.1f));
	EXPECT_EQ(0, dut.determineLockedGear(3.15f));
	EXPECT_EQ(2, dut.determineLockedGear(1.95f));
	EXPECT_EQ(0, dut.determineLockedGear(2.5f));
	EXPECT_EQ(0, dut.determineLockedGear(0));

	// Close ratio gears 10% apart: tolerance shrinks to 2.5%
	engineConfiguration->gearRatio[0] = 1.10f;
	engineConfiguration->gearRatio[1] = 1.00f;
	dut.onConfigurationChange(nullptr);

	EXPECT_EQ(1, dut.determineLockedGear(1.12f));
	EXPECT_EQ(0, dut.determineLockedGear(1.13f));
	EXPECT_EQ(2, dut.determineLockedGear(1.02f));
	EXPECT_EQ(0, dut.determineLockedGear(1.03f));
}

namespace {
// Ratios chosen so that driveshaft RPM = 40 * kph
// Gears: 3.0, 2.0, 1.5, 1.2, 1.0
struct ClutchGearHarness {
	EngineTestHelper eth{engine_type_e::TEST_ENGINE};
	GearDetector& dut;

	ClutchGearHarness()
		: dut(engine->module<GearDetector>().unmock()) {
		engineConfiguration->driveWheelRevPerKm = 600;
		engineConfiguration->finalGearRatio = 4.0f;
		engineConfiguration->totalGearsCount = 5;
		engineConfiguration->gearRatio[0] = 3.0f;
		engineConfiguration->gearRatio[1] = 2.0f;
		engineConfiguration->gearRatio[2] = 1.5f;
		engineConfiguration->gearRatio[3] = 1.2f;
		engineConfiguration->gearRatio[4] = 1.0f;
		engineConfiguration->clutchAppliedBelow = 15;
		engineConfiguration->clutchReleasedAbove = 60;
		dut.onConfigurationChange(nullptr);
	}

	// One 50ms slow callback tick
	size_t step(float rpm, float kph, float clutch) {
		Sensor::setMockValue(SensorType::Rpm, rpm);
		Sensor::setMockValue(SensorType::VehicleSpeed, kph);
		Sensor::setMockValue(SensorType::ClutchPosition, clutch);

		advanceTimeUs(50'000);
		dut.onSlowCallback();

		return dut.get().Value;
	}

	// Sweep the gearbox ratio linearly at constant speed, returning every gear reported
	std::set<size_t> sweep(float fromRatio, float toRatio, float clutch, float seconds, float kph = 50) {
		std::set<size_t> seen;
		int ticks = std::max(1, static_cast<int>(std::round(seconds / 0.05f)));

		for (int i = 0; i < ticks; i++) {
			float ratio = ticks == 1 ? toRatio : fromRatio + (toRatio - fromRatio) * i / (ticks - 1);
			seen.insert(step(ratio * 40 * kph, kph, clutch));
		}

		return seen;
	}

	std::set<size_t> hold(float ratio, float clutch, float seconds, float kph = 50) {
		return sweep(ratio, ratio, clutch, seconds, kph);
	}

	// Drive one full clutch cycle into the gear with this ratio: pedal down with the engine
	// free-revving, then pedal up in gear.
	void establishTrust(float ratio) {
		hold(5.0f, 100, 0.3f);
		hold(ratio, 0, 0.3f);
	}
};
} // namespace

using Gears = std::set<size_t>;

TEST(GearDetector, ClutchNoSensorIsRatioOnly) {
	ClutchGearHarness h;
	Sensor::setInvalidMockValue(SensorType::ClutchPosition);

	// Same late-reapply as the upshift test below, but with no clutch sensor every transient
	// gear shows up.
	Sensor::setMockValue(SensorType::Rpm, 1.0f * 40 * 50);
	Sensor::setMockValue(SensorType::VehicleSpeed, 50);
	advanceTimeUs(50'000);
	h.dut.onSlowCallback();

	EXPECT_EQ(5, h.dut.get().Value);
	EXPECT_FALSE(h.dut.clutchTrusted);
	EXPECT_EQ(static_cast<uint8_t>(ClutchState::Indeterminate), h.dut.clutchState);
}

TEST(GearDetector, ClutchUntrustedUntilFullCycle) {
	ClutchGearHarness h;

	// Pedal up, in 3rd: no cycle seen yet, so ratio-only
	EXPECT_EQ(Gears({3}), h.hold(1.5f, 0, 0.5f));
	EXPECT_FALSE(h.dut.clutchTrusted);

	// A transient is reported as-is while untrusted
	EXPECT_EQ(Gears({4}), h.hold(1.2f, 0, 0.05f));

	h.establishTrust(1.5f);
	EXPECT_TRUE(h.dut.clutchTrusted);
	EXPECT_EQ(3, h.dut.get().Value);
}

TEST(GearDetector, ClutchUpshiftLateReapply) {
	ClutchGearHarness h;
	h.establishTrust(1.5f);
	ASSERT_TRUE(h.dut.clutchTrusted);

	EXPECT_EQ(Gears({3}), h.hold(1.5f, 0, 0.5f));

	// Clutch in, revs fall through both 4th and 5th: hold 3rd
	EXPECT_EQ(Gears({3}), h.sweep(1.5f, 0.9f, 100, 0.4f));

	// Coming back up through the middle, still slipping
	EXPECT_EQ(Gears({3}), h.sweep(0.9f, 1.0f, 40, 0.2f));

	// Pedal fully up but the clutch is still catching up: briefly at the 5th gear ratio
	EXPECT_EQ(Gears({3}), h.hold(1.0f, 0, 0.1f));
	EXPECT_EQ(Gears({3}), h.sweep(1.05f, 1.15f, 0, 0.1f));

	// Locks into 4th: 3 -> 4 with no 5 in between
	EXPECT_EQ(Gears({3, 4}), h.hold(1.2f, 0, 0.3f));
	EXPECT_EQ(4, h.dut.get().Value);
	EXPECT_TRUE(h.dut.clutchTrusted);
}

TEST(GearDetector, ClutchDownshiftOverBlip) {
	ClutchGearHarness h;
	h.establishTrust(1.2f);
	ASSERT_TRUE(h.dut.clutchTrusted);
	EXPECT_EQ(4, h.dut.get().Value);

	// Clutch in and blip way past 3rd, through 2nd: hold 4th (the gear autoblip shifts down from)
	EXPECT_EQ(Gears({4}), h.sweep(1.2f, 2.2f, 100, 0.3f));
	EXPECT_EQ(Gears({4}), h.hold(2.0f, 100, 0.2f));

	// Pedal up with revs still too high
	EXPECT_EQ(Gears({4}), h.hold(2.0f, 0, 0.1f));
	EXPECT_EQ(Gears({4}), h.sweep(1.9f, 1.6f, 0, 0.1f));

	// Settles into 3rd
	EXPECT_EQ(Gears({3, 4}), h.hold(1.5f, 0, 0.3f));
	EXPECT_EQ(3, h.dut.get().Value);
}

TEST(GearDetector, ClutchNeutral) {
	ClutchGearHarness h;
	h.establishTrust(1.5f);
	ASSERT_TRUE(h.dut.clutchTrusted);

	// Pedal up, not in any gear: hold briefly, then neutral
	EXPECT_EQ(Gears({3}), h.hold(1.35f, 0, 0.5f));
	EXPECT_EQ(Gears({0, 3}), h.hold(1.35f, 0, 1.0f));
	EXPECT_EQ(0, h.dut.get().Value);

	// Back into gear
	EXPECT_EQ(Gears({0, 4}), h.hold(1.2f, 0, 0.3f));
	EXPECT_EQ(4, h.dut.get().Value);
}

TEST(GearDetector, ClutchStopped) {
	ClutchGearHarness h;
	h.establishTrust(1.5f);
	ASSERT_TRUE(h.dut.clutchTrusted);

	EXPECT_EQ(0, h.step(800, 0, 100));
	EXPECT_EQ(0, h.step(800, 0, 0));
}

TEST(GearDetector, ClutchInCoastingDoesNotDistrust) {
	ClutchGearHarness h;
	h.establishTrust(1.5f);
	ASSERT_TRUE(h.dut.clutchTrusted);

	// Clutch in, engine idling, slowing from 50 to 20 kph over 10 seconds. Ratio slowly passes
	// through 5th and dwells there for a while, but engine speed is steady so it isn't a real lock.
	Gears seen;
	for (int i = 0; i < 200; i++) {
		float kph = 50 - 30 * i / 200.0f;
		seen.insert(h.step(900, kph, 100));
	}

	EXPECT_EQ(Gears({3}), seen);
	EXPECT_TRUE(h.dut.clutchTrusted);
}

TEST(GearDetector, ClutchStuckPressed) {
	ClutchGearHarness h;
	h.establishTrust(1.5f);
	ASSERT_TRUE(h.dut.clutchTrusted);

	// The sensor says pressed, but the car accelerates in 3rd with engine speed tracking road
	// speed: the clutch is clearly engaged.
	for (int i = 0; i < 20; i++) {
		float kph = 50 + i;
		h.step(1.5f * 40 * kph, kph, 100);
	}

	EXPECT_FALSE(h.dut.clutchTrusted);
	EXPECT_EQ(3, h.dut.get().Value);

	// Untrusted: ratio-only, shifts show up immediately
	EXPECT_EQ(Gears({4}), h.hold(1.2f, 100, 0.05f));
}

TEST(GearDetector, ClutchStuckMiddle) {
	ClutchGearHarness h;
	h.establishTrust(1.5f);
	ASSERT_TRUE(h.dut.clutchTrusted);

	// Pedal stuck partway. A short lock isn't believed...
	EXPECT_EQ(Gears({3}), h.hold(1.2f, 40, 0.5f));

	// ...but one that holds for a while is a real shift
	EXPECT_EQ(Gears({3, 4}), h.hold(1.2f, 40, 1.0f));
	EXPECT_EQ(4, h.dut.get().Value);
}

TEST(GearDetector, ClutchTrustRegained) {
	ClutchGearHarness h;
	h.establishTrust(1.5f);
	ASSERT_TRUE(h.dut.clutchTrusted);

	// Lose trust: pressed but coupled
	for (int i = 0; i < 20; i++) {
		float kph = 50 + i;
		h.step(1.5f * 40 * kph, kph, 100);
	}
	ASSERT_FALSE(h.dut.clutchTrusted);

	// Sensor starts behaving again
	h.establishTrust(1.2f);
	EXPECT_TRUE(h.dut.clutchTrusted);
	EXPECT_EQ(4, h.dut.get().Value);
}

TEST(GearDetector, ClutchSensorFault) {
	ClutchGearHarness h;
	h.establishTrust(1.5f);
	ASSERT_TRUE(h.dut.clutchTrusted);

	Sensor::setMockValue(SensorType::Rpm, 1.2f * 40 * 50);
	Sensor::setInvalidMockValue(SensorType::ClutchPosition);
	advanceTimeUs(50'000);
	h.dut.onSlowCallback();

	EXPECT_FALSE(h.dut.clutchTrusted);
	EXPECT_EQ(4, h.dut.get().Value);
}
