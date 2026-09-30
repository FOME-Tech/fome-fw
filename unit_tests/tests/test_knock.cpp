#include "pch.h"

#include "knock_logic.h"

struct MockKnockController : public KnockControllerBase {
	float getKnockThreshold() const override {
		// Knock threshold of 20dBv
		return 20;
	}

	float getMaximumRetard() const override {
		// Maximum 8 degrees retarded
		return 8;
	}
};

TEST(Knock, EachCylinderGainAndMaximumRetardHaveIndependentSources) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setCylinderCount(2);
	Sensor::setMockValue(SensorType::Rpm, 2000);
	Sensor::setMockValue(SensorType::Map, 80);
	Sensor::setMockValue(SensorType::Tps1, 20);
	engine->engineState.ignitionLoad = 90;
	setArrayValues(config->knockBaseNoise, 20);
	setLinearCurve(config->knockGainLoadBins, 0, 100, 1);
	setLinearCurve(config->maxKnockRetardLoadBins, 0, 100, 1);
	for (size_t row = 0; row < efi::size(config->knockGains[0].table); row++) {
		setArrayValues(config->knockGains[0].table[row], config->knockGainLoadBins[row] / 10);
		setArrayValues(config->knockGains[1].table[row], config->knockGainLoadBins[row] / 10);
	}
	for (size_t row = 0; row < efi::size(config->maxKnockRetardTable); row++) {
		setArrayValues(config->maxKnockRetardTable[row], config->maxKnockRetardLoadBins[row] / 10);
	}
	config->knockGainLoadSource[0] = AFR_Tps;
	config->knockGainLoadSource[1] = AFR_MAP;
	config->knockRetardLoadSource = AFR_Tps;
	KnockController dut;
	dut.onFastCallback();
	EXPECT_FALSE(dut.onKnockSenseCompleted(0, 0, 15, 0));
	EXPECT_TRUE(dut.onKnockSenseCompleted(1, 0, 15, 0));
	const float tpsLimit = dut.getMaximumRetard();
	EXPECT_LT(tpsLimit, 3);
	config->knockRetardLoadSource = AFR_MAP;
	EXPECT_GT(dut.getMaximumRetard(), 7);
}

TEST(Knock, Retards) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	// Aggression of 10%
	engineConfiguration->knockRetardAggression = 10;

	MockKnockController dut;
	dut.onFastCallback();

	// No retard unless we knock
	ASSERT_FLOAT_EQ(dut.getKnockRetard(), 0);

	// Send some weak knocks, should yield no response
	for (size_t i = 0; i < 10; i++) {
		dut.onKnockSenseCompleted(0, 0, 10, 0);
	}

	EXPECT_FLOAT_EQ(dut.getKnockRetard(), 0);

	// Send a strong knock!
	dut.onKnockSenseCompleted(0, 0, 30, 0);

	// Should retard 10% of the distance between current timing and "maximum"
	EXPECT_FLOAT_EQ(dut.getKnockRetard(), 2);

	// Send tons of strong knocks, make sure we don't go over the configured limit
	for (size_t i = 0; i < 100; i++) {
		dut.onKnockSenseCompleted(0, 0, 30, 0);
	}

	EXPECT_FLOAT_EQ(dut.getKnockRetard(), 8);
}

TEST(Knock, Reapply) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	MockKnockController dut;
	dut.onFastCallback();

	// Aggression of 10%
	engineConfiguration->knockRetardAggression = 10;
	// Apply 1 degree/second
	engineConfiguration->knockRetardReapplyRate = 1;

	// Send a strong knock!
	dut.onKnockSenseCompleted(0, 0, 30, 0);

	// Should retard 10% of the distance between current timing and "maximum"
	EXPECT_FLOAT_EQ(dut.getKnockRetard(), 2);

	constexpr auto fastPeriodSec = FAST_CALLBACK_PERIOD_MS / 1000.0f;

	// call the fast callback, should reapply 1 degree * callback period
	dut.onFastCallback();
	EXPECT_FLOAT_EQ(dut.getKnockRetard(), 2 - 1.0f * fastPeriodSec);

	// 10 updates total
	for (size_t i = 0; i < 9; i++) {
		dut.onFastCallback();
	}
	EXPECT_FLOAT_EQ(dut.getKnockRetard(), 2 - 10 * 1.0f * fastPeriodSec);

	// Spend a long time without knock
	for (size_t i = 0; i < 1000; i++) {
		dut.onFastCallback();
	}

	// Should have no knock retard
	EXPECT_FLOAT_EQ(dut.getKnockRetard(), 0);
}
