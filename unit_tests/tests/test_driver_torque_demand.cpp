#include "pch.h"

namespace {
// The driver torque table reuses the ETB pedal-table axes (RPM x pedal).
void setupAxes() {
	copyArray(config->pedalToTpsRpmBins, {0, 1000, 2000, 3000, 4000, 5000, 6000, 7000});
	copyArray(config->pedalToTpsPedalBins, {0, 10, 20, 30, 40, 50, 60, 70});
}

// Fill the (signed) torque table as torquePerPedal * pedal, flat across RPM.
void fillTablePedalScaled(int torquePerPedal) {
	for (int p = 0; p < PEDAL_TO_TPS_SIZE; p++) {
		for (int r = 0; r < PEDAL_TO_TPS_SIZE; r++) {
			config->driverTorqueTable[p][r] = torquePerPedal * config->pedalToTpsPedalBins[p];
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
	// The logged state mirrors the returned demand.
	EXPECT_NEAR(engine->module<TorqueModel>()->driverTorqueDemand, 250, 0.5);
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
