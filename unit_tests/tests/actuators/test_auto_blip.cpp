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
