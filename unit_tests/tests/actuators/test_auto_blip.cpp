#include "pch.h"

TEST(AutoBlip, blipAllowed) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	auto dut = *engine->module<AutoBlip>();

	auto& cfg = engineConfiguration->autoBlip;

	cfg.minCurrentRpm = 2000;
	cfg.minTargetRpm = 3000;
	cfg.minVehicleSpeed = 30;
	cfg.minTargetGear = 2;
	cfg.minClt = 60;

	// Everything OK -> allowed
	Sensor::setMockValue(SensorType::Clt, 65);
	EXPECT_TRUE(dut.blipAllowed(2, 2100, 3100, 35));

	// Target gear too low
	EXPECT_FALSE(dut.blipAllowed(1, 2100, 3100, 35));

	// Current RPM too low
	EXPECT_FALSE(dut.blipAllowed(2, 1900, 3100, 35));

	// Target RPM too low
	EXPECT_FALSE(dut.blipAllowed(2, 2100, 2900, 35));

	// Vehicle speed too low
	EXPECT_FALSE(dut.blipAllowed(2, 2100, 3100, 25));

	// Coolant temp too low
	Sensor::setMockValue(SensorType::Clt, 55);
	EXPECT_FALSE(dut.blipAllowed(2, 2100, 3100, 35));
}

// Zero out the blipAllowed gating thresholds so these state-machine-focused tests aren't coupled to
// whatever default values ship in engine_configuration.cpp - those are covered by the blipAllowed test.
static void clearBlipAllowedThresholds() {
	auto& cfg = engineConfiguration->autoBlip;
	cfg.minCurrentRpm = 0;
	cfg.minTargetRpm = 0;
	cfg.minTargetGear = 0;
	cfg.minVehicleSpeed = 0;
	cfg.minClt = 0;
}

TEST(AutoBlip, disabledNeverBlips) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	auto dut = *engine->module<AutoBlip>();
	Sensor::setMockValue(SensorType::Clt, 65);
	clearBlipAllowedThresholds();
	engineConfiguration->autoBlip.blipThrottleAdd = 20;
	engineConfiguration->autoBlip.enabled = false;

	// Brake, then clutch, exactly as a real blip would be triggered - but the feature is off.
	engine->engineState.brakePedalState = true;
	engine->engineState.clutchDownState = false;
	dut.onFastCallback();
	engine->engineState.clutchDownState = true;
	dut.onFastCallback();
	EXPECT_EQ(dut.getEtbAdjustment(), 0);
}

TEST(AutoBlip, armingRequiresClutchUp) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	auto dut = *engine->module<AutoBlip>();
	Sensor::setMockValue(SensorType::Clt, 65);
	clearBlipAllowedThresholds();
	engineConfiguration->autoBlip.enabled = true;
	engineConfiguration->autoBlip.blipThrottleAdd = 20;

	// Clutch is already down when the brake is applied - this should NOT arm/blip, even after
	// several callbacks, because arming requires the clutch to be up at the moment the brake goes down.
	engine->engineState.brakePedalState = true;
	engine->engineState.clutchDownState = true;
	dut.onFastCallback();
	EXPECT_EQ(dut.getEtbAdjustment(), 0);
	dut.onFastCallback();
	EXPECT_EQ(dut.getEtbAdjustment(), 0);

	// Driver releases the clutch while still braking - this arms the system.
	engine->engineState.clutchDownState = false;
	dut.onFastCallback();
	EXPECT_EQ(dut.getEtbAdjustment(), 0);

	// Now pushing the clutch triggers the blip.
	engine->engineState.clutchDownState = true;
	dut.onFastCallback();
	EXPECT_GT(dut.getEtbAdjustment(), 0);
}

TEST(AutoBlip, armTimeoutPreventsStaleBlip) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	auto dut = *engine->module<AutoBlip>();
	Sensor::setMockValue(SensorType::Clt, 65);
	clearBlipAllowedThresholds();
	engineConfiguration->autoBlip.enabled = true;
	engineConfiguration->autoBlip.blipThrottleAdd = 20;
	engineConfiguration->autoBlip.armTimeout = 0.2f;

	// Brake applied with clutch up - arms.
	engine->engineState.brakePedalState = true;
	engine->engineState.clutchDownState = false;
	dut.onFastCallback();

	// Wait past the arm timeout without ever pressing the clutch.
	advanceTimeUs(0.3e6);
	dut.onFastCallback();

	// A clutch press now is stale - it must not fire a blip, even though we're still braking.
	engine->engineState.clutchDownState = true;
	dut.onFastCallback();
	EXPECT_EQ(dut.getEtbAdjustment(), 0);

	// Releasing and reapplying the brake resets everything, so a fresh arm+blip works again.
	engine->engineState.brakePedalState = false;
	dut.onFastCallback();
	engine->engineState.brakePedalState = true;
	engine->engineState.clutchDownState = false;
	dut.onFastCallback();
	engine->engineState.clutchDownState = true;
	dut.onFastCallback();
	EXPECT_GT(dut.getEtbAdjustment(), 0);
}
