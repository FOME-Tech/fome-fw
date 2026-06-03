#include "pch.h"

namespace {
// The driver torque table is indexed [pedal][rpm] on its own dedicated axes.
void setupAxes() {
	copyArray(config->driverTorqueRpmBins, {0, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000, 11000});
	copyArray(config->driverTorquePedalBins, {0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110});
}

// Fill the (signed) torque table as torquePerPedal * pedal, flat across RPM.
void fillTablePedalScaled(int torquePerPedal) {
	for (size_t p = 0; p < efi::size(config->driverTorquePedalBins); p++) {
		for (size_t r = 0; r < efi::size(config->driverTorqueRpmBins); r++) {
			config->driverTorqueTable[p][r] = torquePerPedal * config->driverTorquePedalBins[p];
		}
	}
}
} // namespace

TEST(DriverTorqueDemand, InterpolatesPositiveDemand) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setupAxes();
	fillTablePedalScaled(10); // pedal 25 -> 250 Nm

	Sensor::setMockValue(SensorType::Rpm, 3500);
	Sensor::setMockValue(SensorType::AcceleratorPedal, 25);

	EXPECT_NEAR(engine->module<TorqueModel>()->driverDemand(), 250, 0.5);
}

TEST(DriverTorqueDemand, HandlesSignedDemand) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setupAxes();
	fillTablePedalScaled(-5); // pedal 50 -> -250 Nm (engine braking / overrun)

	Sensor::setMockValue(SensorType::Rpm, 2000);
	Sensor::setMockValue(SensorType::AcceleratorPedal, 50);

	EXPECT_NEAR(engine->module<TorqueModel>()->driverDemand(), -250, 0.5);
}

TEST(DriverTorqueDemand, DefaultsToZero) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setupAxes();
	// Table left at its zero default.

	Sensor::setMockValue(SensorType::Rpm, 4000);
	Sensor::setMockValue(SensorType::AcceleratorPedal, 40);

	EXPECT_NEAR(engine->module<TorqueModel>()->driverDemand(), 0, 0.001);
}
