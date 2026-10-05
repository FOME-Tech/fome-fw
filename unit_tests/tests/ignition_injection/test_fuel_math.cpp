#include "pch.h"
#include "fuel_math.h"
#include "alphan_airmass.h"
#include "maf_airmass.h"
#include "speed_density_airmass.h"

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
	setLinearCurve(config->mafTrimLoadBins, 0, 140, 1); // 0, 20, 40 ... 140
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

void setInjectionMode(int value);

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
