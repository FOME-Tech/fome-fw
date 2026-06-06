#include "pch.h"

#include "traction_control.h"

namespace {
// Drive the real GearDetector into a single known gear; total ratio at the engine is gear1 *
// finalDrive
void detectGear(float gear1, float finalDrive) {
	engineConfiguration->driveWheelRevPerKm = 600;
	engineConfiguration->finalGearRatio = finalDrive;
	engineConfiguration->totalGearsCount = 1;
	engineConfiguration->gearRatio[0] = gear1;

	auto& gd = engine->module<GearDetector>().unmock();
	gd.onConfigurationChange(nullptr);

	Sensor::setMockValue(SensorType::VehicleSpeed, 50);
	Sensor::setMockValue(SensorType::Rpm, gear1 * 500 * finalDrive);
	gd.onSlowCallback();
}

void detectNeutral() {
	engineConfiguration->driveWheelRevPerKm = 600;
	engineConfiguration->finalGearRatio = 4;
	engineConfiguration->totalGearsCount = 1;
	engineConfiguration->gearRatio[0] = 2;

	auto& gd = engine->module<GearDetector>().unmock();
	gd.onConfigurationChange(nullptr);

	Sensor::setMockValue(SensorType::VehicleSpeed, 0);
	Sensor::setMockValue(SensorType::Rpm, 800);
	gd.onSlowCallback();
}

// Front-driven by default: driven = {LF, RF}, reference = {LR, RR}.
void setWheels(float front, float rear) {
	Sensor::setMockValue(SensorType::WheelSpeedLF, front);
	Sensor::setMockValue(SensorType::WheelSpeedRF, front);
	Sensor::setMockValue(SensorType::WheelSpeedLR, rear);
	Sensor::setMockValue(SensorType::WheelSpeedRR, rear);
}

// Flat slip-target table at `target`, sane gains, front-driven.
void configureTc(float target) {
	engineConfiguration->enableTractionControl = true;
	auto& tc = engineConfiguration->tractionControl;
	tc.drivenAxleIsFront = true; // driven = {LF, RF}, reference = {LR, RR}
	tc.minimumSpeed = 5;
	tc.engineTorqueFloor = 0;
	tc.engageRate = 0; // unlimited bite
	tc.releaseRate = 2000;
	tc.slipTargetMax = 100;
	tc.slipTargetYAxis = GPPWM_Zero;
	tc.slipPid.pFactor = 50;
	tc.slipPid.iFactor = 100;
	tc.slipPid.dFactor = 0;
	tc.slipPid.offset = 0;
	tc.slipPid.minValue = 0;
	tc.slipPid.maxValue = 30000;

	setLinearCurve(config->slipTargetSpeedBins, 0, 200, 1);
	setLinearCurve(config->slipTargetTrimBins, 0, 3, 1);
	for (size_t s = 0; s < efi::size(config->slipTargetSpeedBins); s++) {
		for (size_t t = 0; t < efi::size(config->slipTargetTrimBins); t++) {
			config->slipTargetTable[s][t] = target;
		}
	}
}
} // namespace

// ============================ slip measurement ============================

TEST(TractionControl, SlipNoneWhenEqual) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureTc(10);
	setWheels(50, 50);

	TractionController tc;
	auto slip = tc.getSlip();
	ASSERT_TRUE(slip.Valid);
	EXPECT_NEAR(slip.Value.slipPercent, 0, 1e-3);
	EXPECT_NEAR(slip.Value.drivenSpeed, 50, 1e-3);
	EXPECT_NEAR(slip.Value.referenceSpeed, 50, 1e-3);
}

TEST(TractionControl, SlipPositiveWhenDrivenFaster) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureTc(10);
	setWheels(60, 50); // front (driven) 20% faster than rear (reference)

	TractionController tc;
	auto slip = tc.getSlip();
	ASSERT_TRUE(slip.Valid);
	EXPECT_NEAR(slip.Value.slipPercent, 20, 1e-3);
}

TEST(TractionControl, SlipDegradesToSurvivingDrivenWheel) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureTc(10);
	setWheels(60, 50);
	Sensor::setInvalidMockValue(SensorType::WheelSpeedRF); // one driven wheel drops out

	TractionController tc;
	auto slip = tc.getSlip();
	ASSERT_TRUE(slip.Valid);
	EXPECT_NEAR(slip.Value.slipPercent, 20, 1e-3); // still 60 from the surviving LF
}

TEST(TractionControl, SlipDisarmsWithoutReference) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureTc(10);
	setWheels(60, 50);
	Sensor::setInvalidMockValue(SensorType::WheelSpeedLR);
	Sensor::setInvalidMockValue(SensorType::WheelSpeedRR);

	TractionController tc;
	EXPECT_FALSE(tc.getSlip().Valid);
}

TEST(TractionControl, SlipDisarmsBelowMinimumSpeed) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureTc(10);
	setWheels(4, 3); // reference below minimumSpeed = 5

	TractionController tc;
	EXPECT_FALSE(tc.getSlip().Valid);
}

// One driven wheel (LF) peels to 60 while its mate (RF) stays planted at 50, reference 50.
// Average reads ((60+50)/2 - 50)/50 = 10%; Fastest reads (60 - 50)/50 = 20%, catching the peel.
TEST(TractionControl, DrivenAxleCombineModes) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureTc(10);
	Sensor::setMockValue(SensorType::WheelSpeedLF, 60); // peeling
	Sensor::setMockValue(SensorType::WheelSpeedRF, 50); // planted
	Sensor::setMockValue(SensorType::WheelSpeedLR, 50);
	Sensor::setMockValue(SensorType::WheelSpeedRR, 50);

	TractionController tc;

	engineConfiguration->tractionControl.drivenSlipUseFastest = false;
	auto avg = tc.getSlip();
	ASSERT_TRUE(avg.Valid);
	EXPECT_NEAR(avg.Value.slipPercent, 10, 1e-3);
	EXPECT_NEAR(avg.Value.drivenSpeed, 55, 1e-3);

	engineConfiguration->tractionControl.drivenSlipUseFastest = true;
	auto fast = tc.getSlip();
	ASSERT_TRUE(fast.Valid);
	EXPECT_NEAR(fast.Value.slipPercent, 20, 1e-3);
	EXPECT_NEAR(fast.Value.drivenSpeed, 60, 1e-3);
	// Reference axle ignores the mode - still the mean of the (equal) undriven wheels.
	EXPECT_NEAR(fast.Value.referenceSpeed, 50, 1e-3);
}

// ============================ target lookup ============================

TEST(TractionControl, TargetInterpolatesAndClamps) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureTc(0);
	// Ramp target with speed: 0 kph -> 4%, 200 kph -> 18%.
	config->slipTargetSpeedBins[0] = 0;
	config->slipTargetSpeedBins[1] = 100;
	for (size_t t = 0; t < efi::size(config->slipTargetTrimBins); t++) {
		config->slipTargetTable[0][t] = 4;
		config->slipTargetTable[1][t] = 18;
	}

	TractionController tc;
	EXPECT_NEAR(tc.getTargetSlip(0), 4, 0.5);
	EXPECT_NEAR(tc.getTargetSlip(50), 11, 0.5); // halfway between 4 and 18

	engineConfiguration->tractionControl.slipTargetMax = 10;
	EXPECT_NEAR(tc.getTargetSlip(50), 10, 0.5); // clamped
}

TEST(TractionControl, TargetTrimAxisSelectsColumn) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureTc(0);
	engineConfiguration->tractionControl.slipTargetYAxis = GPPWM_Tps;
	engineConfiguration->tractionControl.slipTargetMax = 100;
	setLinearCurve(config->slipTargetTrimBins, 0, 30, 1); // X axis 0..30 across 4 columns
	for (size_t s = 0; s < efi::size(config->slipTargetSpeedBins); s++) {
		config->slipTargetTable[s][0] = 5;	// trim = 0
		config->slipTargetTable[s][3] = 25; // trim = 30
	}

	TractionController tc;
	Sensor::setMockValue(SensorType::Tps1, 0);
	EXPECT_NEAR(tc.getTargetSlip(50), 5, 0.5);
	Sensor::setMockValue(SensorType::Tps1, 30);
	EXPECT_NEAR(tc.getTargetSlip(50), 25, 0.5);
}

// ============================ controller / arming ============================

TEST(TractionControl, DisarmedWhenDisabled) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureTc(10);
	engineConfiguration->enableTractionControl = false;
	detectGear(2, 4);
	setWheels(60, 50);

	TractionController tc;
	EXPECT_FALSE(tc.getTorqueLimit(200).Valid);
}

TEST(TractionControl, DisarmedWithoutGearRatio) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureTc(10);
	detectNeutral();
	setWheels(60, 50);

	TractionController tc;
	EXPECT_FALSE(tc.getTorqueLimit(200).Valid);
}

TEST(TractionControl, TransparentWhenNoSlip) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureTc(10);
	detectGear(2, 4);  // ratio 8
	setWheels(50, 50); // no slip

	TractionController tc;
	// Grip in hand at any pedal: the ceiling rails at the request, TC stays out of the way.
	auto high = tc.getTorqueLimit(200);
	ASSERT_TRUE(high.Valid);
	EXPECT_NEAR(high.Value, 200, 0.5);

	auto low = tc.getTorqueLimit(50);
	ASSERT_TRUE(low.Valid);
	EXPECT_NEAR(low.Value, 50, 0.5);
}

// A fast tip-in with full grip must be tracked immediately - the released ceiling follows the
// pedal with no lag (regression for integrator-lag / rate-limit clipping the driver).
TEST(TractionControl, TipInTracksImmediately) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureTc(10);
	detectGear(2, 4);  // ratio 8
	setWheels(50, 50); // no slip

	TractionController tc;
	tc.getTorqueLimit(100); // settle, released at the rail

	// Pedal stab from 100 -> 400 Nm, still zero slip: must return the new request this very tick.
	auto r = tc.getTorqueLimit(400);
	ASSERT_TRUE(r.Valid);
	EXPECT_NEAR(r.Value, 400, 0.5);
}

TEST(TractionControl, BitesWhenSlipping) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureTc(10);
	detectGear(2, 4);
	setWheels(60, 50); // 20% slip, target 10%

	TractionController tc;
	float limit = 200;
	for (int i = 0; i < 10; i++) {
		limit = tc.getTorqueLimit(200).value_or(200);
	}
	EXPECT_LT(limit, 200);
}

// Once limiting, the controller must keep holding even if slip momentarily falls back to target.
// A naive "pin when slip <= target" gate would snap the integrator to the rail and dump the cut;
// gating on the clip state (output still below the rail) holds the bite and releases only at the
// rate limit.
TEST(TractionControl, HoldsBiteWhenSlipReturnsToTarget) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureTc(10);
	detectGear(2, 4);

	TractionController tc;
	setWheels(70, 50); // 40% slip -> hard bite
	for (int i = 0; i < 20; i++) {
		tc.getTorqueLimit(200);
	}

	// Slip falls exactly to target: still actively limiting, nowhere near the full request.
	setWheels(55, 50); // slip == target (10%)
	auto held = tc.getTorqueLimit(200);
	ASSERT_TRUE(held.Valid);
	EXPECT_LT(held.Value, 100);
}

// §4.1: while slipping, the axle ceiling is set by the slip loop, not the pedal. Doubling the
// request must not move the published axle ceiling.
TEST(TractionControl, ThrottleUpDoesNotRaiseAxleCeiling) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureTc(10);
	detectGear(2, 4);
	setWheels(60, 50);

	auto runFinal = [](float finalRequest) {
		TractionController tc;
		for (int i = 0; i < 3; i++) {
			tc.getTorqueLimit(200);
		}
		tc.getTorqueLimit(finalRequest);
		return (float)tc.axleTorqueLimit;
	};

	EXPECT_NEAR(runFinal(200), runFinal(400), 1.0f);
}

// The rate limit only governs the slip-cleared handback (rise); the bite (fall) is unlimited.
TEST(TractionControl, ReleaseIsRateLimited) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureTc(10);
	detectGear(2, 4);
	engineConfiguration->tractionControl.releaseRate = 2000; // axle Nm/s

	TractionController tc;
	setWheels(60, 50);
	for (int i = 0; i < 5; i++) {
		tc.getTorqueLimit(200);
	}
	float before = tc.axleTorqueLimit;
	ASSERT_LT(before, 1600); // biting (rail = 200 * 8)

	// Slip clears: ceiling wants to snap back to the rail but may only rise releaseRate * dt.
	setWheels(50, 50);
	tc.getTorqueLimit(200);
	float after = tc.axleTorqueLimit;

	float maxRise = 2000 * (FAST_CALLBACK_PERIOD_MS / 1000.0f);
	EXPECT_GT(after, before);
	EXPECT_NEAR(after, before + maxRise, 1.0f);
	EXPECT_LT(after, 1600); // did NOT snap to the rail
}
