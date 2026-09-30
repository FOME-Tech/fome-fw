#include "pch.h"

#include "closed_loop_fuel_cell.h"
#include "closed_loop_fuel.h"

using ::testing::_;
using ::testing::Return;
using ::testing::StrictMock;

class MockClCell : public ClosedLoopFuelCellBase {
public:
	MOCK_METHOD(float, getLambdaError, (), (const));
	MOCK_METHOD(float, getMaxAdjustment, (), (const));
	MOCK_METHOD(float, getMinAdjustment, (), (const));
	MOCK_METHOD(float, getIntegratorGain, (), (const));
};

TEST(ClosedLoopFuelCell, AdjustRate) {
	StrictMock<MockClCell> cl;

	EXPECT_CALL(cl, getLambdaError()).WillOnce(Return(0.1f));
	EXPECT_CALL(cl, getMinAdjustment()).WillOnce(Return(-0.2f));
	EXPECT_CALL(cl, getMaxAdjustment()).WillOnce(Return(0.2f));
	EXPECT_CALL(cl, getIntegratorGain()).WillOnce(Return(2.0f));

	cl.update(false);

	// Should have integrated 0.2 * dt
	// dt = 1000.0f / FAST_CALLBACK_PERIOD_MS
	EXPECT_FLOAT_EQ(cl.getAdjustment(), 1 + (0.2f / (1000.0f / FAST_CALLBACK_PERIOD_MS)));
}

TEST(ClosedLoopFuel, CellSelection) {
	stft_s cfg;

	// Sensible region config
	cfg.maxIdleRegionRpm = 1500;
	cfg.minPowerLoad = 80;
	cfg.maxOverrunLoad = 30;

	// Test idle
	EXPECT_EQ(0, computeStftBin(1000, 10, cfg));
	EXPECT_EQ(0, computeStftBin(1000, 50, cfg));
	EXPECT_EQ(0, computeStftBin(1000, 90, cfg));

	// Test overrun
	EXPECT_EQ(1, computeStftBin(2000, 10, cfg));
	EXPECT_EQ(1, computeStftBin(4000, 10, cfg));
	EXPECT_EQ(1, computeStftBin(10000, 10, cfg));

	// Test load
	EXPECT_EQ(2, computeStftBin(2000, 90, cfg));
	EXPECT_EQ(2, computeStftBin(4000, 90, cfg));
	EXPECT_EQ(2, computeStftBin(10000, 90, cfg));

	// Main cell
	EXPECT_EQ(3, computeStftBin(2000, 50, cfg));
	EXPECT_EQ(3, computeStftBin(4000, 50, cfg));
	EXPECT_EQ(3, computeStftBin(10000, 50, cfg));
}

TEST(ClosedLoopFuel, SelectedRegionLoadIsIndependentOfFuelAndLambdaLoads) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engine->rpmCalculator.setRpmValue(2000);
	engineConfiguration->fuelClosedLoopCorrectionEnabled = true;
	engineConfiguration->stft.startupDelay = 0;
	engineConfiguration->stft.minClt = 0;
	engineConfiguration->stft.maxIdleRegionRpm = 1000;
	engineConfiguration->stft.maxOverrunLoad = 30;
	engineConfiguration->stft.minPowerLoad = 70;
	engine->engineState.fuelingLoad = 80;
	Sensor::setMockValue(SensorType::Map, 80);
	Sensor::setMockValue(SensorType::Tps1, 20);
	config->stftLoadSource = AFR_Tps;
	fuelClosedLoopCorrection();
	EXPECT_EQ(engine->outputChannels.fuelClosedLoopBinIdx, 1);

	engineConfiguration->afrOverrideMode = AFR_CylFilling;
	fuelClosedLoopCorrection();
	EXPECT_EQ(engine->outputChannels.fuelClosedLoopBinIdx, 1);
	config->stftLoadSource = AFR_MAP;
	fuelClosedLoopCorrection();
	EXPECT_EQ(engine->outputChannels.fuelClosedLoopBinIdx, 2);
}

TEST(ClosedLoopFuel, lambdaLimits) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	engineConfiguration->stft.minLambda = 0.7f;
	engineConfiguration->stft.maxLambda = 1.2f;

	// Lower bound
	Sensor::setMockValue(SensorType::Lambda1, 0.65f);
	EXPECT_FALSE(shouldUpdateCorrection(SensorType::Lambda1));
	Sensor::setMockValue(SensorType::Lambda1, 0.75f);
	EXPECT_TRUE(shouldUpdateCorrection(SensorType::Lambda1));

	Sensor::setMockValue(SensorType::Lambda1, 1.15f);
	EXPECT_TRUE(shouldUpdateCorrection(SensorType::Lambda1));
	Sensor::setMockValue(SensorType::Lambda1, 1.25f);
	EXPECT_FALSE(shouldUpdateCorrection(SensorType::Lambda1));
}
