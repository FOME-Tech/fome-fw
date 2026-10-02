#include "pch.h"

#include "knock_logic.h"
#include "airmass_loads.h"

struct MockKnockController : public KnockControllerBase {
	mutable unsigned thresholdReads = 0;
	mutable unsigned maximumReads = 0;
	float getKnockThreshold() const override {
		thresholdReads++;
		// Knock threshold of 20dBv
		return 20;
	}

	float getMaximumRetard() const override {
		maximumReads++;
		// Maximum 8 degrees retarded
		return 8;
	}
};

TEST(Knock, TwelveCylinderGainsMatchDirectInterpolationAndRefreshEachCallback) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->enableSoftwareKnock = true;
	setCylinderCount(12);
	engineConfiguration->fuelAlgorithm = LM_MOCK;
	Sensor::setMockValue(SensorType::Rpm, 2000);
	Sensor::setMockValue(SensorType::Map, 80);
	Sensor::setMockValue(SensorType::Tps1, 25);
	Sensor::setMockValue(SensorType::AcceleratorPedal, 40);
	engine->engineState.fuelingLoad = 60;
	engine->fuelComputer.normalizedCylinderFilling = 70;
	setLinearCurve(config->knockGainLoadBins, 0, 150, 1);
	setLinearCurve(config->knockGainRpmBins, 500, 5000, 1);
	const load_override_e sources[] = {AFR_Tps, AFR_MAP, AFR_AccPedal, AFR_None, AFR_CylFilling, AFR_EffectiveMAP};
	for (size_t cylinder = 0; cylinder < 12; cylinder++) {
		config->knockGainLoadSource[cylinder] = sources[cylinder % efi::size(sources)];
		for (size_t row = 0; row < efi::size(config->knockGains[cylinder].table); row++) {
			for (size_t column = 0; column < efi::size(config->knockGains[cylinder].table[row]); column++) {
				config->knockGains[cylinder].table[row][column] = (cylinder * 3 + row * 7 + column * 11) % 31;
			}
		}
	}
	MockKnockController dut;
	const auto check = [&] {
		dut.onFastCallback();
		for (size_t cylinder = 0; cylinder < 12; cylinder++) {
			const int8_t gain = interpolate3d(
					config->knockGains[cylinder].table,
					config->knockGainLoadBins,
					getAirmassConsumerLoad(AirmassConsumer::KnockGain, cylinder),
					config->knockGainRpmBins,
					Sensor::getOrZero(SensorType::Rpm));
			EXPECT_FALSE(dut.onKnockSenseCompleted(cylinder, 0, 20 - gain, 0)) << "cylinder=" << cylinder;
			EXPECT_TRUE(dut.onKnockSenseCompleted(cylinder, 0, 20.125f - gain, 0)) << "cylinder=" << cylinder;
		}
	};
	check();
	config->knockGainLoadBins[1] = 35;
	config->knockGainRpmBins[1] = 2500;
	config->knockGains[7].table[1][1] = 29;
	config->knockGainLoadSource[7] = AFR_Tps;
	Sensor::setMockValue(SensorType::Tps1, 35);
	check();
	for (auto& source : config->knockGainLoadSource) {
		source = AFR_MAP;
	}
	check();
}

TEST(Knock, EachCylinderGainAndMaximumRetardHaveIndependentSources) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->enableSoftwareKnock = true;
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
	engineConfiguration->enableSoftwareKnock = true;

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
	engineConfiguration->enableSoftwareKnock = true;

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

TEST(Knock, DisabledSkipsCalibrationButDecaysAndProcessesPendingSenseWithRetainedGains) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->fuelAlgorithm = LM_MOCK;
	engineConfiguration->enableSoftwareKnock = true;
	engineConfiguration->knockRetardAggression = 10;
	engineConfiguration->knockRetardReapplyRate = 1;
	Sensor::setMockValue(SensorType::Rpm, 2000);
	setTable(config->knockGains[0].table, 5);
	MockKnockController dut;
	dut.onFastCallback();
	ASSERT_TRUE(dut.onKnockSenseCompleted(0, 0, 16, getTimeNowNt()));
	ASSERT_FLOAT_EQ(dut.getKnockRetard(), 2);
	engineConfiguration->enableSoftwareKnock = false;
	config->knockGainLoadSource[0] = static_cast<load_override_e>(255);
	setTable(config->knockGains[0].table, 30);
	resetAirmassCursorPreparationCounts();
	dut.onFastCallback();
	EXPECT_EQ(dut.thresholdReads, 1u);
	EXPECT_EQ(dut.maximumReads, 1u);
	EXPECT_EQ(getAirmassConsumerReadCount(AirmassConsumer::KnockGain), 0u);
	EXPECT_FLOAT_EQ(dut.getKnockRetard(), 2 - FAST_CALLBACK_PERIOD_MS / 1000.0f);
	EXPECT_TRUE(dut.hasKnockRetardNow);
	EXPECT_TRUE(dut.hasKnockRecently);
	// A completion accepted before disable still uses its last threshold, gain and limit.
	for (unsigned i = 0; i < 10; i++) {
		EXPECT_TRUE(dut.onKnockSenseCompleted(0, 0, 16, getTimeNowNt()));
	}
	EXPECT_FLOAT_EQ(dut.getKnockRetard(), 8);
	eth.moveTimeForwardMs(600);
	dut.onFastCallback();
	EXPECT_FALSE(dut.hasKnockRecently);
	EXPECT_TRUE(dut.hasKnockRetardNow);
	engineConfiguration->enableSoftwareKnock = true;
	config->knockGainLoadSource[0] = AFR_None;
	setTable(config->knockGains[0].table, 0);
	dut.onFastCallback();
	EXPECT_EQ(dut.thresholdReads, 2u);
	EXPECT_EQ(dut.maximumReads, 2u);
	EXPECT_FALSE(dut.onKnockSenseCompleted(0, 0, 16, getTimeNowNt()));
}
