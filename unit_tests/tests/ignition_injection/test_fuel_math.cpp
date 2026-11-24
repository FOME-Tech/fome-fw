#include "pch.h"
#include "fuel_math.h"
#include "alphan_airmass.h"
#include "maf_airmass.h"
#include "speed_density_airmass.h"
#include "speed_density_base.h"

using ::testing::_;
using ::testing::FloatNear;
using ::testing::InSequence;
using ::testing::StrictMock;

TEST(FuelMath, getStandardAirCharge) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	// Miata 1839cc 4cyl
	engineConfiguration->displacement = 1.839f;
	setCylinderCount(4);

	EXPECT_FLOAT_EQ(0.5535934f, getStandardAirCharge());

	// LS 5.3 liter v8
	engineConfiguration->displacement = 5.327f;
	setCylinderCount(8);

	EXPECT_FLOAT_EQ(0.80179232f, getStandardAirCharge());

	// Chainsaw - single cylinder 32cc
	engineConfiguration->displacement = 0.032f;
	setCylinderCount(1);
	EXPECT_FLOAT_EQ(0.038531788f, getStandardAirCharge());

	// Leopard 1 47.666 liter v12
	engineConfiguration->displacement = 47.666f;
	setCylinderCount(12);

	EXPECT_FLOAT_EQ(4.782959f, getStandardAirCharge());
}

TEST(AirmassModes, AlphaNNormal) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	// 4 cylinder 4 liter = easy math
	engineConfiguration->displacement = 4.0f;
	setCylinderCount(4);

	StrictMock<MockVp3d> veTable;

	EXPECT_CALL(veTable, getValue(1200, FloatNear(0.71f, EPS4D))).WillOnce(Return(35.0f));

	AlphaNAirmass dut(&veTable);

	// that's 0.71% not 71%
	Sensor::setMockValue(SensorType::Tps1, 0.71f);

	// Mass of 1 liter of air * VE
	mass_t expectedAirmass = 1.2047f * 0.35f;

	auto result = dut.getAirmass(1200, false).value_or({});
	EXPECT_NEAR(result.CylinderAirmass, expectedAirmass, EPS4D);
	EXPECT_NEAR(result.EngineLoadPercent, 0.71f, EPS4D);
}

TEST(AirmassModes, AlphaNUseIat) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	// 4 cylinder 4 liter = easy math
	engineConfiguration->displacement = 4.0f;
	setCylinderCount(4);

	StrictMock<MockVp3d> veTable;

	EXPECT_CALL(veTable, getValue(1200, FloatNear(0.71f, EPS4D))).WillRepeatedly(Return(35.0f));

	AlphaNAirmass dut(&veTable);

	// that's 0.71% not 71%
	Sensor::setMockValue(SensorType::Tps1, 0.71f);

	// Mass of 1 liter of air * VE
	mass_t expectedAirmass = 1.2047f * 0.35f;

	EXPECT_NEAR(dut.getAirmass(1200, false).value_or({}).CylinderAirmass, expectedAirmass, EPS4D);

	engineConfiguration->alphaNUseIat = true;

	// Cold we get more airmass
	float expectedAirmassCold = expectedAirmass * (273.0f + 20) / (273.0f + 0);
	Sensor::setMockValue(SensorType::Iat, 0);
	EXPECT_NEAR(dut.getAirmass(1200, false).value_or({}).CylinderAirmass, expectedAirmassCold, EPS4D);

	// Hot we get less airmass
	float expectedAirmassHot = expectedAirmass * (273.0f + 20) / (273.0f + 40);
	Sensor::setMockValue(SensorType::Iat, 40);
	EXPECT_NEAR(dut.getAirmass(1200, false).value_or({}).CylinderAirmass, expectedAirmassHot, EPS4D);
}

TEST(AirmassModes, AlphaNFailedTps) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	// Shouldn't get called
	StrictMock<MockVp3d> veTable;

	AlphaNAirmass dut(&veTable);

	// explicitly reset the sensor
	Sensor::resetMockValue(SensorType::Tps1);
	// Ensure that it's actually failed
	ASSERT_FALSE(Sensor::get(SensorType::Tps1).Valid);

	auto result = dut.getAirmass(1200, false);
	EXPECT_FALSE(result.Valid);
}

TEST(AirmassModes, MafNormal) {
	EngineTestHelper eth(engine_type_e::FORD_ASPIRE_1996);
	engineConfiguration->fuelAlgorithm = LM_REAL_MAF;
	engineConfiguration->injector.flow = 200;

	// MAF uses its own trim table, the VE table should never be read
	StrictMock<MockVp3d> veTable;
	MafAirmass dut(&veTable);

	// Default trim table is all 100%, aka no correction
	{
		auto airmass = dut.getAirmassImpl(200, 6000);
		ASSERT_TRUE(airmass.Valid);
		EXPECT_NEAR(0.277777f, airmass.Value.CylinderAirmass, EPS4D);
		EXPECT_NEAR(70.9814f, airmass.Value.EngineLoadPercent, EPS4D);
	}

	// Trim table that varies differently along each axis, to check that the correct cell is read
	setLinearCurve(config->mafTrimLoadBins, 0, 140, 1);	   // 0, 20, 40 ... 140
	setLinearCurve(config->mafTrimRpmBins, 1000, 8000, 1); // 1000, 2000, 3000 ... 8000
	for (size_t loadIdx = 0; loadIdx < efi::size(config->mafTrimLoadBins); loadIdx++) {
		for (size_t rpmIdx = 0; rpmIdx < efi::size(config->mafTrimRpmBins); rpmIdx++) {
			config->mafTrimTable[loadIdx][rpmIdx] = 50 + 5 * loadIdx + 10 * rpmIdx;
		}
	}

	{
		auto airmass = dut.getAirmassImpl(200, 6000);
		ASSERT_TRUE(airmass.Valid);

		// Load 70.98 is load index 3.549, 6000 RPM is RPM index 5
		float expectedTrim = (50 + 5 * (70.9814f / 20) + 10 * 5) / 100;
		EXPECT_NEAR(0.277777f * expectedTrim, airmass.Value.CylinderAirmass, EPS4D);

		// Trim doesn't affect load
		EXPECT_NEAR(70.9814f, airmass.Value.EngineLoadPercent, EPS4D);
	}
}

TEST(AirmassModes, MafMaxLoad) {
	EngineTestHelper eth(engine_type_e::FORD_ASPIRE_1996);
	engineConfiguration->fuelAlgorithm = LM_REAL_MAF;
	engineConfiguration->displacement = 2.0f;
	setCylinderCount(4);

	MafAirmass dut;

	copyArray(config->mafMaxLoadRpmBins, {500, 1000, 2000, 3000, 4000, 5000, 6000, 7000});
	copyArray(config->mafMaxLoad, {200, 100, 100, 100, 100, 100, 100, 100});

	Sensor::setMockValue(SensorType::BarometricPressure, 100);
	Sensor::setMockValue(SensorType::Iat, 20);

	// Ideal 0.5L cylinder at 100kPa, 20C
	float idealAirmass = idealGasLaw(0.5f, 100, 273.15f + 20);

	// 240 kg/h at 2000 rpm is 1g per cylinder
	auto flowForAirmass = [](float airmass, float rpm) { return airmass * 2 * (rpm / 60) * 3.6f; };

	// Below the limit: unchanged
	{
		auto airmass = dut.getAirmassImpl(flowForAirmass(0.5f, 2000), 2000);
		ASSERT_TRUE(airmass.Valid);
		EXPECT_NEAR(0.5f, airmass.Value.CylinderAirmass, EPS4D);
		EXPECT_FALSE(engine->outputChannels.mafAirmassLimited);
	}

	// Above the limit: clamped to 100% of an ideal cylinder at baro and IAT, and so is the load
	{
		auto airmass = dut.getAirmassImpl(flowForAirmass(1.0f, 2000), 2000);
		ASSERT_TRUE(airmass.Valid);
		EXPECT_NEAR(idealAirmass, airmass.Value.CylinderAirmass, EPS4D);
		EXPECT_NEAR(100 * idealAirmass / getStandardAirCharge(), airmass.Value.EngineLoadPercent, EPS4D);
		EXPECT_TRUE(engine->outputChannels.mafAirmassLimited);
	}

	// Limit follows baro and IAT
	{
		Sensor::setMockValue(SensorType::BarometricPressure, 80);
		Sensor::setMockValue(SensorType::Iat, 50);

		auto airmass = dut.getAirmassImpl(flowForAirmass(1.0f, 2000), 2000);
		ASSERT_TRUE(airmass.Valid);
		EXPECT_NEAR(idealGasLaw(0.5f, 80, 273.15f + 50), airmass.Value.CylinderAirmass, EPS4D);
	}

	// No baro sensor: assume 101.325kPa
	{
		Sensor::setInvalidMockValue(SensorType::BarometricPressure);
		Sensor::setMockValue(SensorType::Iat, 20);

		auto airmass = dut.getAirmassImpl(flowForAirmass(1.0f, 2000), 2000);
		ASSERT_TRUE(airmass.Valid);
		EXPECT_NEAR(idealGasLaw(0.5f, 101.325f, 273.15f + 20), airmass.Value.CylinderAirmass, EPS4D);
	}

	// Limit is interpolated by RPM: 750 rpm is halfway between 200% and 100%
	{
		auto airmass = dut.getAirmassImpl(flowForAirmass(2.0f, 750), 750);
		ASSERT_TRUE(airmass.Valid);
		EXPECT_NEAR(1.5f * idealGasLaw(0.5f, 101.325f, 273.15f + 20), airmass.Value.CylinderAirmass, EPS4D);
	}

	// Limit applies before the MAF trim
	{
		setTable(config->mafTrimTable, 110);

		auto airmass = dut.getAirmassImpl(flowForAirmass(1.0f, 2000), 2000);
		ASSERT_TRUE(airmass.Valid);
		EXPECT_NEAR(1.1f * idealGasLaw(0.5f, 101.325f, 273.15f + 20), airmass.Value.CylinderAirmass, EPS4D);
	}
}

TEST(AirmassModes, MafMaxLoadDefaultDoesNotLimit) {
	EngineTestHelper eth(engine_type_e::FORD_ASPIRE_1996);
	engineConfiguration->fuelAlgorithm = LM_REAL_MAF;
	engineConfiguration->displacement = 2.0f;
	setCylinderCount(4);

	MafAirmass dut;

	// 3x an ideal cylinder, e.g. a boosted engine
	float airmass = 3 * idealGasLaw(0.5f, 101.325f, 273.15f + 20);
	auto result = dut.getAirmassImpl(airmass * 2 * (3000.0f / 60) * 3.6f, 3000);
	ASSERT_TRUE(result.Valid);
	EXPECT_NEAR(airmass, result.Value.CylinderAirmass, EPS4D);
	EXPECT_FALSE(engine->outputChannels.mafAirmassLimited);
}

// Intake manifold filter tests: 2.0L 4 cylinder, time constant 600 / RPM seconds
class MafAirmassFilter : public ::testing::Test {
protected:
	void SetUp() override {
		engineConfiguration->fuelAlgorithm = LM_REAL_MAF;
		engineConfiguration->displacement = 2.0f;
		setCylinderCount(4);
		engineConfiguration->mafFilterParameter = 600;
	}

	// MAF flow (kg/h) that reads as this per-cylinder airmass (g) at this RPM
	static float flowFor(float airmass, float rpm) {
		return airmass * 2 * (rpm / 60) * 3.6f;
	}

	// Run the fuel calculation's update of the airmass model for this long, at the fast callback rate
	float runFor(float seconds, float rpm, float flow) {
		Sensor::setMockValue(SensorType::Maf, flow);

		float result = 0;
		int steps = seconds * 1000 / FAST_CALLBACK_PERIOD_MS;
		for (int i = 0; i < steps; i++) {
			advanceTimeUs(FAST_CALLBACK_PERIOD_MS * 1000);
			result = dut.getAirmass(rpm, true).value_or(AirmassResult{-1, 0}).CylinderAirmass;
		}

		return result;
	}

	EngineTestHelper eth{engine_type_e::FORD_ASPIRE_1996};
	MafAirmass dut;
};

TEST_F(MafAirmassFilter, StartsFromFirstMeasurement) {
	Sensor::setMockValue(SensorType::Maf, flowFor(0.3f, 200));
	EXPECT_NEAR(0.3f, dut.getAirmass(200, true).value_or(AirmassResult{}).CylinderAirmass, 1e-4);
}

TEST_F(MafAirmassFilter, TimeConstantIsParameterOverRpm) {
	runFor(0.1f, 600, flowFor(0.2f, 600));

	// Step to 0.4g at 600 rpm: 1 second time constant, so 1 - 1/e of the way after 1 second
	float expected = 0.2f + 0.2f * (1 - expf(-1));
	EXPECT_NEAR(expected, runFor(1, 600, flowFor(0.4f, 600)), 0.002f);

	// Settles on the measurement
	EXPECT_NEAR(0.4f, runFor(10, 600, flowFor(0.4f, 600)), 1e-4);
}

TEST_F(MafAirmassFilter, RpmRiseHoldsAirmass) {
	// Cranking at 200 rpm, manifold full: 0.3g per cylinder
	runFor(1, 200, flowFor(0.3f, 200));

	// The engine catches: RPM jumps 6x, but the flow past the throttle hasn't changed, so the
	// MAF alone says 0.05g per cylinder. The manifold is still full, so the airmass can't drop
	// that fast.
	float flow = flowFor(0.3f, 200);
	EXPECT_NEAR(0.05f, dut.getAirmassImpl(flow, 1200).value_or(AirmassResult{}).CylinderAirmass, 1e-4);

	float afterOneStep = runFor(FAST_CALLBACK_PERIOD_MS / 1000, 1200, flow);
	EXPECT_NEAR(0.3f, afterOneStep, 0.003f);

	// ...then it empties with a 600 / 1200 = 0.5 second time constant
	float expected = 0.05f + 0.25f * expf(-1);
	EXPECT_NEAR(expected, runFor(0.5f, 1200, flow), 0.003f);
}

TEST_F(MafAirmassFilter, RpmDropDoesntOverRead) {
	// Running at 1000 rpm, 0.1g per cylinder
	runFor(5, 1000, flowFor(0.1f, 1000));

	// The engine dies to 50 rpm while the MAF still reads the old flow: MAF alone says 2g
	float flow = flowFor(0.1f, 1000);
	EXPECT_NEAR(2.0f, dut.getAirmassImpl(flow, 50).value_or(AirmassResult{}).CylinderAirmass, 1e-3);

	// The filter barely moves at 50 rpm (12 second time constant)
	EXPECT_NEAR(0.1f + 1.9f * (1 - expf(-0.1f / 12)), runFor(0.1f, 50, flow), 0.002f);
}

TEST_F(MafAirmassFilter, ResetWhenStopped) {
	runFor(1, 600, flowFor(0.2f, 600));

	Sensor::setMockValue(SensorType::Maf, flowFor(0.2f, 600));
	EXPECT_FALSE(dut.getAirmass(0, true).Valid);

	// Starts over from the next measurement
	Sensor::setMockValue(SensorType::Maf, flowFor(0.5f, 200));
	advanceTimeUs(FAST_CALLBACK_PERIOD_MS * 1000);
	EXPECT_NEAR(0.5f, dut.getAirmass(200, true).value_or(AirmassResult{}).CylinderAirmass, 1e-4);
}

TEST_F(MafAirmassFilter, OtherCallersDontAdvanceFilter) {
	runFor(1, 600, flowFor(0.2f, 600));

	// Lua asking for airmass sees the filtered value, and doesn't move the filter
	Sensor::setMockValue(SensorType::Maf, flowFor(0.4f, 600));
	for (int i = 0; i < 100; i++) {
		advanceTimeUs(FAST_CALLBACK_PERIOD_MS * 1000);
		EXPECT_NEAR(0.2f, dut.getAirmass(600, false).value_or(AirmassResult{}).CylinderAirmass, 1e-4);
	}

	// The next fuel calculation catches up for all the time since the last one
	advanceTimeUs(FAST_CALLBACK_PERIOD_MS * 1000);
	float elapsed = 101 * FAST_CALLBACK_PERIOD_MS / 1000;
	float alpha = elapsed * 600 / 600;
	EXPECT_NEAR(0.2f + 0.2f * alpha, dut.getAirmass(600, true).value_or(AirmassResult{}).CylinderAirmass, 1e-4);
}

TEST_F(MafAirmassFilter, ZeroParameterDisablesFilter) {
	engineConfiguration->mafFilterParameter = 0;

	runFor(1, 600, flowFor(0.2f, 600));
	EXPECT_NEAR(0.4f, runFor(FAST_CALLBACK_PERIOD_MS / 1000, 600, flowFor(0.4f, 600)), 1e-4);
}

TEST_F(MafAirmassFilter, MaxLoadLimitsFilterInput) {
	setArrayValues(config->mafMaxLoad, 100);
	Sensor::setMockValue(SensorType::BarometricPressure, 100);
	Sensor::setMockValue(SensorType::Iat, 20);
	float maxAirmass = idealGasLaw(0.5f, 100, 273.15f + 20);

	runFor(1, 3000, flowFor(0.2f, 3000));

	// A long spike well over the limit only pulls the filter up to the limit
	EXPECT_NEAR(maxAirmass, runFor(10, 3000, flowFor(5.0f, 3000)), 1e-4);
	EXPECT_TRUE(engine->outputChannels.mafAirmassLimited);
}

// Logged starts, 2.5L 4 cylinder E85, CLT 5.6C: RPM and unfiltered MAF (kg/h) every 40ms
struct MafLogSample {
	float rpm;
	float maf;
};

// Cranking, then the engine catches and flares to 2250 rpm
static const MafLogSample mafLogCatch[] = {
		{162, 7.8f},   {214, 7.6f},	  {256, 7.6f},	 {200, 9.2f},	{221, 9.0f},   {248, 8.0f},	  {212, 8.8f},
		{184, 9.7f},   {227, 8.8f},	  {225, 8.4f},	 {194, 9.6f},	{205, 9.5f},   {252, 8.3f},	  {217, 9.0f},
		{188, 9.7f},   {450, 9.1f},	  {394, 10.4f},	 {602, 11.1f},	{600, 12.2f},  {716, 13.8f},  {874, 15.2f},
		{999, 17.2f},  {1230, 20.2f}, {1353, 21.8f}, {1520, 24.1f}, {1681, 25.2f}, {1832, 26.2f}, {1943, 26.8f},
		{2033, 27.3f}, {2112, 27.8f}, {2173, 28.1f}, {2214, 28.3f}, {2246, 28.4f}, {2263, 28.4f}, {2255, 28.4f},
		{2252, 28.9f}, {2231, 28.8f}, {2208, 28.8f}, {2164, 29.2f}, {2136, 29.3f}, {2080, 29.3f}, {2015, 29.4f},
		{1943, 29.8f}, {1900, 29.9f}, {1807, 30.3f}, {1746, 30.3f}};

// A weak catch that dies: RPM falls to 55 while the MAF reading lags
static const MafLogSample mafLogStall[] = {
		{296, 7.4f},  {263, 8.3f},	{223, 9.1f},  {452, 9.1f},	{388, 10.3f}, {368, 11.1f}, {276, 12.5f},
		{418, 12.2f}, {368, 13.4f}, {587, 13.9f}, {551, 14.3f}, {492, 15.5f}, {427, 16.5f}, {571, 16.7f},
		{525, 17.3f}, {451, 17.7f}, {454, 17.7f}, {345, 18.6f}, {347, 17.8f}, {288, 17.6f}, {176, 18.2f},
		{174, 16.5f}, {262, 14.4f}, {188, 14.9f}, {145, 14.8f}, {128, 10.5f}, {169, 6.9f},	{149, 5.6f},
		{133, 4.6f},  {126, 5.2f},	{126, 6.5f},  {126, 6.7f},	{65, 5.5f},	  {64, 6.0f},	{55, 7.1f},
		{55, 7.6f},	  {55, 7.4f},	{55, 10.2f},  {55, 7.9f},	{55, 6.6f},	  {55, 6.2f}};

class MafAirmassLogReplay : public MafAirmassFilter {
protected:
	void SetUp() override {
		MafAirmassFilter::SetUp();
		engineConfiguration->displacement = 2.494f;
		engineConfiguration->mafFilterParameter = 160;
	}

	struct Result {
		float filtered;
		float unfiltered;
	};

	// Hold each 40ms log sample for 10 fast callbacks
	template <size_t N>
	std::vector<Result> replay(const MafLogSample (&log)[N]) {
		std::vector<Result> results;
		for (auto& sample : log) {
			float filtered = runFor(0.04f, sample.rpm, sample.maf);
			float unfiltered = dut.getAirmassImpl(sample.maf, sample.rpm).value_or(AirmassResult{}).CylinderAirmass;
			results.push_back({filtered, unfiltered});
		}
		return results;
	}
};

TEST_F(MafAirmassLogReplay, Catch) {
	auto results = replay(mafLogCatch);

	float crankingAirmass = results[0].filtered;

	for (size_t i = 0; i < results.size(); i++) {
		// Never more than the cylinder held while cranking, with the manifold full
		EXPECT_LE(results[i].filtered, crankingAirmass * 1.05f) << i;
	}

	// The first 0.2 seconds after reaching 600 rpm: the manifold is still emptying, so the MAF
	// alone reads under half the airmass
	for (size_t i = 17; i <= 22; i++) {
		EXPECT_GT(results[i].filtered, 1.4f * results[i].unfiltered) << i;
	}

	// Settled at 2000 rpm: filter has caught up with the MAF
	EXPECT_NEAR(results.back().unfiltered, results.back().filtered, 0.1f * results.back().unfiltered);
}

TEST_F(MafAirmassLogReplay, Stall) {
	auto results = replay(mafLogStall);

	float maxFiltered = 0;
	float maxUnfiltered = 0;
	for (auto& r : results) {
		maxFiltered = std::max(maxFiltered, r.filtered);
		maxUnfiltered = std::max(maxUnfiltered, r.unfiltered);
	}

	// The MAF alone says over 1g per cylinder at 55 rpm, more than the cylinder can hold
	EXPECT_GT(maxUnfiltered, 1.0f);

	// The filtered airmass stays around what the cylinder held while running
	EXPECT_LT(maxFiltered, 0.5f);
}

TEST(AirmassModes, MafFailed) {
	EngineTestHelper eth(engine_type_e::FORD_ASPIRE_1996);
	engineConfiguration->fuelAlgorithm = LM_REAL_MAF;

	MafAirmass dut;

	ASSERT_FALSE(Sensor::hasSensor(SensorType::Maf2));

	// Working MAF
	Sensor::setMockValue(SensorType::Maf, 200);
	EXPECT_TRUE(dut.getAirmass(6000, false).Valid);

	// Engine stopped -> fail
	EXPECT_FALSE(dut.getAirmass(0, false).Valid);

	// Dead MAF -> fail
	Sensor::setInvalidMockValue(SensorType::Maf);
	EXPECT_FALSE(dut.getAirmass(6000, false).Valid);
}

TEST(AirmassModes, MafDual) {
	EngineTestHelper eth(engine_type_e::FORD_ASPIRE_1996);
	engineConfiguration->fuelAlgorithm = LM_REAL_MAF;

	MafAirmass dut;

	// Each case should total 200kg/h, same as MafNormal
	auto checkValid = [&]() {
		auto airmass = dut.getAirmass(6000, false);
		ASSERT_TRUE(airmass.Valid);
		EXPECT_NEAR(0.277777f, airmass.Value.CylinderAirmass, EPS4D);
	};

	// Both working -> sum
	Sensor::setMockValue(SensorType::Maf, 100);
	Sensor::setMockValue(SensorType::Maf2, 100);
	checkValid();

	// MAF 2 dead -> double MAF 1
	Sensor::setInvalidMockValue(SensorType::Maf2);
	checkValid();

	// MAF 1 dead -> double MAF 2
	Sensor::setInvalidMockValue(SensorType::Maf);
	Sensor::setMockValue(SensorType::Maf2, 100);
	checkValid();

	// Both dead -> fail
	Sensor::setInvalidMockValue(SensorType::Maf2);
	EXPECT_FALSE(dut.getAirmass(6000, false).Valid);
}

TEST(AirmassModes, SpeedDensityFailed) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	MockVp3d veTable;
	StrictMock<MockVp3d> mapFallback;
	SpeedDensityAirmass dut(&veTable, mapFallback);

	EXPECT_CALL(veTable, getValue(_, _)).WillRepeatedly(Return(80.0f));

	// Working case
	engine->engineState.sd.tChargeK = 273.15f + 20;
	EXPECT_TRUE(dut.getAirmass(3000, 100, false).Valid);
	EXPECT_GT(dut.getAirflow(3000, 100, false), 0);

	// tCharge not ready -> fail
	engine->engineState.sd.tChargeK = NAN;
	EXPECT_FALSE(dut.getAirmass(3000, 100, false).Valid);
	EXPECT_EQ(0, dut.getAirflow(3000, 100, false));

	// NaN airmass -> fail
	engine->engineState.sd.tChargeK = 273.15f + 20;
	EXPECT_CALL(veTable, getValue(_, _)).WillRepeatedly(Return(NAN));
	EXPECT_FALSE(dut.getAirmass(3000, 100, false).Valid);
	EXPECT_EQ(0, dut.getAirflow(3000, 100, false));
}

TEST(AirmassModes, VeOverride) {
	StrictMock<MockVp3d> veTable;

	{
		InSequence is;

		// Default
		EXPECT_CALL(veTable, getValue(_, 10.0f)).WillOnce(Return(0));
		// TPS
		EXPECT_CALL(veTable, getValue(_, 30.0f)).WillOnce(Return(0));
	}

	struct DummyAirmassModel : public AirmassVeModelBase {
		DummyAirmassModel(const ValueProvider3D* veTable)
			: AirmassVeModelBase(veTable) {}

		expected<AirmassResult> getAirmass(float rpm, bool postState) override {
			// Default load value 10, will be overriden
			getVe(rpm, 10.0f, postState, VeTableType::SpeedDensity);

			return AirmassResult{};
		}
	};

	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	DummyAirmassModel dut(&veTable);

	// Use default mode - will call with 10
	dut.getAirmass(0, true);
	EXPECT_FLOAT_EQ(engine->engineState.veTableYAxis, 10.0f);

	// Override to TPS
	engineConfiguration->veOverrideMode = VE_TPS;
	Sensor::setMockValue(SensorType::Tps1, 30.0f);
	dut.getAirmass(0, true);
	EXPECT_FLOAT_EQ(engine->engineState.veTableYAxis, 30.0f);
}

TEST(AirmassModes, FallbackMap) {
	StrictMock<MockVp3d> veTable;
	StrictMock<MockVp3d> mapFallback;

	// Failed map -> use 75
	{
		InSequence is;

		// Working map -> return 33 (should be unused)
		EXPECT_CALL(mapFallback, getValue(1234, 20)).WillOnce(Return(33));

		// Failed map -> use 75
		EXPECT_CALL(mapFallback, getValue(5678, 20)).WillOnce(Return(75));
	}

	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	SpeedDensityAirmass dut(&veTable, mapFallback);

	// TPS at 20%
	Sensor::setMockValue(SensorType::Tps1, 20);

	// Working MAP sensor at 40 kPa
	Sensor::setMockValue(SensorType::Map, 40);
	EXPECT_FLOAT_EQ(dut.getMap(1234, false), 40);

	// Failed MAP sensor, should use table
	Sensor::resetMockValue(SensorType::Map);
	EXPECT_FLOAT_EQ(dut.getMap(5678, false), 75);
}

static void setInjectionMode(int value) {
	engineConfiguration->injectionMode = (injection_mode_e)value;
	incrementGlobalConfigurationVersion();
}

TEST(FuelMath, testDifferentInjectionModes) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setupSimpleTestEngineWithMafAndTT_ONE_trigger(&eth);

	EXPECT_CALL(*eth.mockAirmass, getAirmass(_, _)).WillRepeatedly(Return(AirmassResult{1.3440001f, 50.0f}));

	setInjectionMode((int)IM_BATCH);
	engine->periodicFastCallback();
	EXPECT_FLOAT_EQ(20, engine->engineState.injectionDuration) << "injection while batch";

	setInjectionMode((int)IM_SIMULTANEOUS);
	engine->periodicFastCallback();
	EXPECT_FLOAT_EQ(10, engine->engineState.injectionDuration) << "injection while simultaneous";

	setInjectionMode((int)IM_SEQUENTIAL);
	engine->periodicFastCallback();
	EXPECT_FLOAT_EQ(40, engine->engineState.injectionDuration) << "injection while IM_SEQUENTIAL";

	setInjectionMode((int)IM_SINGLE_POINT);
	engine->periodicFastCallback();
	EXPECT_FLOAT_EQ(40, engine->engineState.injectionDuration) << "injection while IM_SINGLE_POINT";
	EXPECT_EQ(0, eth.recentWarnings()->getCount()) << "warningCounter#testDifferentInjectionModes";
}

TEST(FuelMath, deadtime) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	setupSimpleTestEngineWithMafAndTT_ONE_trigger(&eth);

	EXPECT_CALL(*eth.mockAirmass, getAirmass(_, _)).WillRepeatedly(Return(AirmassResult{1.3440001f, 50.0f}));

	// First test with no deadtime
	engine->periodicFastCallback();
	EXPECT_FLOAT_EQ(20, engine->engineState.injectionDuration);

	// Now add some deadtime
	setArrayValues(engineConfiguration->injector.battLagCorr, 2.0f);

	// Should have deadtime now!
	engine->periodicFastCallback();
	EXPECT_FLOAT_EQ(20 + 2, engine->engineState.injectionDuration);
}

TEST(FuelMath, CylinderFuelTrim) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	EXPECT_CALL(*eth.mockAirmass, getAirmass(_, _)).WillRepeatedly(Return(AirmassResult{1, 50.0f}));

	setTable(config->fuelTrims[0].table, -4);
	setTable(config->fuelTrims[1].table, -2);
	setTable(config->fuelTrims[2].table, 2);
	setTable(config->fuelTrims[3].table, 4);

	// run the fuel math
	engine->periodicFastCallback();

	// Check that each cylinder gets the expected amount of fuel
	float unadjusted = 0.072142f;
	EXPECT_NEAR(engine->cylinders[0].getInjectionMass(), unadjusted * 0.96, EPS4D);
	EXPECT_NEAR(engine->cylinders[1].getInjectionMass(), unadjusted * 0.98, EPS4D);
	EXPECT_NEAR(engine->cylinders[2].getInjectionMass(), unadjusted * 1.02, EPS4D);
	EXPECT_NEAR(engine->cylinders[3].getInjectionMass(), unadjusted * 1.04, EPS4D);
}

TEST(FuelMath, CylinderTrimsUseSeparateAxesAndLiveUpdates) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	EXPECT_CALL(*eth.mockAirmass, getAirmass(_, _)).WillRepeatedly(Return(AirmassResult{1, 65.0f}));
	Sensor::setMockValue(SensorType::Rpm, 3500);
	Sensor::setMockValue(SensorType::Map, 35);
	engineConfiguration->ignOverrideMode = AFR_MAP;

	const uint16_t fuelLoadBins[4] = {0, 50, 100, 150};
	const uint16_t fuelRpmBins[4] = {0, 3000, 6000, 9000};
	const uint16_t ignitionLoadBins[4] = {0, 20, 40, 60};
	const uint16_t ignitionRpmBins[4] = {0, 2000, 4000, 8000};
	copyArray(config->fuelTrimLoadBins, fuelLoadBins);
	copyArray(config->fuelTrimRpmBins, fuelRpmBins);
	copyArray(config->ignTrimLoadBins, ignitionLoadBins);
	copyArray(config->ignTrimRpmBins, ignitionRpmBins);

	for (size_t cylinder = 0; cylinder < 12; cylinder++) {
		for (size_t row = 0; row < 4; row++) {
			for (size_t column = 0; column < 4; column++) {
				config->fuelTrims[cylinder].table[row][column] = static_cast<float>(
						-20 + static_cast<int>(cylinder) * 2 + static_cast<int>(row) * 2 + static_cast<int>(column));
				config->ignTrims[cylinder].table[row][column] = static_cast<float>(
						20 - static_cast<int>(cylinder) * 2 - static_cast<int>(row) - static_cast<int>(column) * 2);
			}
		}
	}

	auto checkTrims = [&] {
		float expectedFuelTrims[12];
		float expectedIgnitionTrims[12];
		for (size_t cylinder = 0; cylinder < engine->engineState.cylinderCount; cylinder++) {
			expectedFuelTrims[cylinder] = (100 + interpolate3d(
														 config->fuelTrims[cylinder].table,
														 config->fuelTrimLoadBins,
														 65.0f,
														 config->fuelTrimRpmBins,
														 3500)) /
										  100;
			expectedIgnitionTrims[cylinder] = interpolate3d(
					config->ignTrims[cylinder].table, config->ignTrimLoadBins, 35.0f, config->ignTrimRpmBins, 3500);
		}

		engine->periodicFastCallback();

		float untrimmedFuel = engine->cylinders[0].getInjectionMass() / expectedFuelTrims[0];
		float untrimmedIgnition = engine->cylinders[0].getIgnitionTimingBtdc() - expectedIgnitionTrims[0];
		for (size_t cylinder = 0; cylinder < engine->engineState.cylinderCount; cylinder++) {
			EXPECT_NEAR(
					engine->cylinders[cylinder].getInjectionMass(), untrimmedFuel * expectedFuelTrims[cylinder], EPS4D);
			EXPECT_FLOAT_EQ(
					engine->cylinders[cylinder].getIgnitionTimingBtdc(),
					untrimmedIgnition + expectedIgnitionTrims[cylinder]);
		}
	};

	// The callback's cylinder loop supports every count up to the configured maximum.
	for (size_t cylinderCount = 1; cylinderCount <= 12; cylinderCount++) {
		engine->engineState.cylinderCount = cylinderCount;
		checkTrims();
	}

	float previousFuel = engine->cylinders[1].getInjectionMass();
	float previousIgnition = engine->cylinders[1].getIgnitionTimingBtdc();
	config->fuelTrimLoadBins[1] = 60;
	checkTrims();
	EXPECT_NE(previousFuel, engine->cylinders[1].getInjectionMass());
	EXPECT_FLOAT_EQ(previousIgnition, engine->cylinders[1].getIgnitionTimingBtdc());

	previousIgnition = engine->cylinders[1].getIgnitionTimingBtdc();
	config->ignTrimRpmBins[2] = 5000;
	checkTrims();
	EXPECT_NE(previousIgnition, engine->cylinders[1].getIgnitionTimingBtdc());
}

struct MockIdle : public MockIdleController {
	bool isIdling = false;

	bool isIdlingOrTaper() const override {
		return isIdling;
	}
};

TEST(FuelMath, IdleVeTable) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	MockAirmass dut;

	// Install mock idle controller
	MockIdle idler;
	engine->engineModules.get<IdleController>().set(&idler);

	// Main VE table returns 50
	EXPECT_CALL(dut.veTable, getValue(_, _)).WillRepeatedly(Return(50));

	// Idle VE table returns 40
	setTable(config->idleVeTable, 40);

	// Enable separate idle VE table
	engineConfiguration->useSeparateVeForIdle = true;
	engineConfiguration->idlePidDeactivationTpsThreshold = 10;

	// Set TPS so this works
	Sensor::setMockValue(SensorType::DriverThrottleIntent, 0);

	// Gets normal VE table
	idler.isIdling = false;
	EXPECT_FLOAT_EQ(dut.getVe(1000, 50, false, VeTableType::SpeedDensity), 0.5f);

	// Gets idle VE table
	idler.isIdling = true;
	EXPECT_FLOAT_EQ(dut.getVe(1000, 50, false, VeTableType::SpeedDensity), 0.4f);

	// Below half threshold, fully use idle VE table
	Sensor::setMockValue(SensorType::DriverThrottleIntent, 0);
	EXPECT_FLOAT_EQ(dut.getVe(1000, 50, false, VeTableType::SpeedDensity), 0.4f);
	Sensor::setMockValue(SensorType::DriverThrottleIntent, 2);
	EXPECT_FLOAT_EQ(dut.getVe(1000, 50, false, VeTableType::SpeedDensity), 0.4f);
	Sensor::setMockValue(SensorType::DriverThrottleIntent, 5);
	EXPECT_FLOAT_EQ(dut.getVe(1000, 50, false, VeTableType::SpeedDensity), 0.4f);

	// As TPS approaches idle threshold, phase-out the idle VE table

	Sensor::setMockValue(SensorType::DriverThrottleIntent, 6);
	EXPECT_FLOAT_EQ(dut.getVe(1000, 50, false, VeTableType::SpeedDensity), 0.42f);
	Sensor::setMockValue(SensorType::DriverThrottleIntent, 8);
	EXPECT_FLOAT_EQ(dut.getVe(1000, 50, false, VeTableType::SpeedDensity), 0.46f);
	Sensor::setMockValue(SensorType::DriverThrottleIntent, 10);
	EXPECT_FLOAT_EQ(dut.getVe(1000, 50, false, VeTableType::SpeedDensity), 0.5f);
}

static void setupVeBlend(blend_table_s& blend, VeTableType consumer, float bias) {
	blend.blendParameter = GPPWM_Clt;
	blend.yAxisOverride = GPPWM_Zero;
	blend.veTableSelect = consumer;

	setLinearCurve(blend.loadBins, 0, 140, 1);
	setLinearCurve(blend.rpmBins, 1000, 8000, 1);
	setTable(blend.table, bias);

	// Blend percent equals CLT, from 0 to 70
	setLinearCurve(blend.blendBins, 0, 70, 1);
	setLinearCurve(blend.blendValues, 0, 70, 1);
}

TEST(FuelMath, VeBlends) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	MockAirmass dut;

	// Main VE table returns 50
	EXPECT_CALL(dut.veTable, getValue(_, _)).WillRepeatedly(Return(50));

	engineConfiguration->veOverrideMode = VE_None;
	engineConfiguration->useSeparateVeForIdle = false;

	for (auto& blend : config->veBlends) {
		blend.blendParameter = GPPWM_Zero;
	}

	Sensor::setMockValue(SensorType::Clt, 50);

	// No blends enabled, plain VE
	EXPECT_NEAR(dut.getVe(1000, 50, false, VeTableType::SpeedDensity), 0.5f, EPS4D);
	EXPECT_NEAR(dut.getVe(1000, 50, false, VeTableType::AlphaN), 0.5f, EPS4D);

	// Blend 1 adds 10% to speed density, blend 2 removes 20% from alpha-N
	setupVeBlend(config->veBlends[0], VeTableType::SpeedDensity, 10);
	setupVeBlend(config->veBlends[1], VeTableType::AlphaN, -20);

	// CLT 50 -> 50% blend, so +5% for SD and -10% for alpha-N
	EXPECT_NEAR(dut.getVe(1000, 50, true, VeTableType::SpeedDensity), 0.5f * 1.05f, EPS4D);
	EXPECT_NEAR(engine->engineState.currentVe, 50 * 1.05f, EPS2D);
	EXPECT_NEAR(engine->outputChannels.veBlendParameter[0], 50, EPS2D);
	EXPECT_NEAR(engine->outputChannels.veBlendBias[0], 50, EPS2D);
	EXPECT_NEAR(engine->outputChannels.veBlendOutput[0], 5, EPS2D);
	EXPECT_NEAR(engine->outputChannels.veBlendYAxis[0], 50, EPS2D);

	EXPECT_NEAR(dut.getVe(1000, 50, true, VeTableType::AlphaN), 0.5f * 0.9f, EPS4D);
	EXPECT_NEAR(engine->engineState.currentVe, 50 * 0.9f, EPS2D);
	EXPECT_NEAR(engine->outputChannels.veBlendOutput[1], -10, EPS2D);

	// CLT 0 -> 0% blend, no effect
	Sensor::setMockValue(SensorType::Clt, 0);
	EXPECT_NEAR(dut.getVe(1000, 50, false, VeTableType::SpeedDensity), 0.5f, EPS4D);
	EXPECT_NEAR(dut.getVe(1000, 50, false, VeTableType::AlphaN), 0.5f, EPS4D);

	// Dead blend parameter sensor -> no effect
	Sensor::setInvalidMockValue(SensorType::Clt);
	EXPECT_NEAR(dut.getVe(1000, 50, false, VeTableType::SpeedDensity), 0.5f, EPS4D);
	EXPECT_NEAR(dut.getVe(1000, 50, false, VeTableType::AlphaN), 0.5f, EPS4D);

	// Both blends applied to speed density: they stack multiplicatively, alpha-N gets neither
	Sensor::setMockValue(SensorType::Clt, 50);
	config->veBlends[1].veTableSelect = VeTableType::SpeedDensity;
	EXPECT_NEAR(dut.getVe(1000, 50, false, VeTableType::SpeedDensity), 0.5f * 1.05f * 0.9f, EPS4D);
	EXPECT_NEAR(dut.getVe(1000, 50, false, VeTableType::AlphaN), 0.5f, EPS4D);

	// Both blends applied to alpha-N
	config->veBlends[0].veTableSelect = VeTableType::AlphaN;
	config->veBlends[1].veTableSelect = VeTableType::AlphaN;
	EXPECT_NEAR(dut.getVe(1000, 50, false, VeTableType::SpeedDensity), 0.5f, EPS4D);
	EXPECT_NEAR(dut.getVe(1000, 50, false, VeTableType::AlphaN), 0.5f * 1.05f * 0.9f, EPS4D);
}
