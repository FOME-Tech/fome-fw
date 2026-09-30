/*
 * @file	test_engine_math.c
 *
 * @date Nov 14, 2013
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"

#include "speed_density.h"
#include "maf.h"
#include "engine_math.h"

TEST(EngineMath, MissingBlendOverrideIsNeutralWithoutZeroCoordinateLookup) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	blend_table_s blend{};
	blend.blendParameter = GPPWM_Clt;
	blend.yAxisOverride = GPPWM_Tps;
	setLinearCurve(blend.rpmBins, 0, 100, 1);
	setLinearCurve(blend.loadBins, 0, 100, 1);
	setLinearCurve(blend.blendBins, 0, 100, 1);
	setArrayValues(blend.blendValues, 100);
	for (size_t row = 0; row < efi::size(blend.table); row++) {
		for (size_t column = 0; column < efi::size(blend.table[row]); column++) {
			blend.table[row][column] = 10 + blend.loadBins[row] * 0.5f + blend.rpmBins[column] * 0.1f;
		}
	}
	Sensor::setMockValue(SensorType::Clt, 70);
	Sensor::setMockValue(SensorType::Tps1, 40);
	const auto valid = calculateBlend(blend, 50, 80);
	EXPECT_NEAR(valid.Value, 35, 0.2f);
	EXPECT_FLOAT_EQ(valid.BlendParameter, 70);
	EXPECT_FLOAT_EQ(valid.Bias, 100);
	EXPECT_FLOAT_EQ(valid.TableYAxis, 40);

	// Looking up the zero row would produce 15, and the parent load would produce 55.
	// An unavailable selected coordinate must produce a neutral correction and diagnostics.
	auto expectNeutral = [&] {
		const auto result = calculateBlend(blend, 50, 80);
		EXPECT_FLOAT_EQ(result.Value, 0);
		EXPECT_FLOAT_EQ(result.BlendParameter, 0);
		EXPECT_FLOAT_EQ(result.Bias, 0);
		EXPECT_FLOAT_EQ(result.TableYAxis, 0);
	};
	Sensor::setInvalidMockValue(SensorType::Tps1);
	expectNeutral();
	Sensor::setMockValue(SensorType::Tps1, NAN);
	expectNeutral();
	Sensor::setMockValue(SensorType::Tps1, INFINITY);
	expectNeutral();
	Sensor::setMockValue(SensorType::Tps1, 40);
	Sensor::setInvalidMockValue(SensorType::Clt);
	expectNeutral();
	Sensor::setMockValue(SensorType::Clt, NAN);
	expectNeutral();
	Sensor::setMockValue(SensorType::Clt, INFINITY);
	expectNeutral();

	// Disabled corrections remain neutral even when their selected inputs are unavailable.
	blend.blendParameter = GPPWM_Zero;
	Sensor::setInvalidMockValue(SensorType::Tps1);
	expectNeutral();
}

TEST(misc, testEngineMath) {
	printf("*************************************************** testEngineMath\r\n");

	// todo: let's see if we can make 'engine' unneeded in this test?
	EngineTestHelper eth(engine_type_e::FORD_ESCORT_GT);

	setCamOperationMode();
	engineConfiguration->fuelAlgorithm = LM_SPEED_DENSITY;

	ASSERT_NEAR(50, getOneDegreeTimeMs(600) * 180, EPS4D) << "600 RPM";
	ASSERT_EQ(5, getOneDegreeTimeMs(6000) * 180) << "6000 RPM";

	auto fuelComputer = &engine->fuelComputer;

	Sensor::setMockValue(SensorType::Clt, 300);
	Sensor::setMockValue(SensorType::Iat, 350);
	ASSERT_FLOAT_EQ(312.5, fuelComputer->getTCharge(1000, 0));
	ASSERT_FLOAT_EQ(313.5833, fuelComputer->getTCharge(1000, 50));
	ASSERT_FLOAT_EQ(314.6667, fuelComputer->getTCharge(1000, 100));

	ASSERT_FLOAT_EQ(312.5, fuelComputer->getTCharge(4000, 0));
	ASSERT_FLOAT_EQ(320.0833, fuelComputer->getTCharge(4000, 50));
	ASSERT_FLOAT_EQ(327.6667, fuelComputer->getTCharge(4000, 100));

	// test Air Interpolation mode
	engineConfiguration->tChargeMode = TCHARGE_MODE_AIR_INTERP;
	engineConfiguration->tChargeAirCoefMin = 0.098f;
	engineConfiguration->tChargeAirCoefMax = 0.902f;
	engineConfiguration->tChargeAirFlowMax = 153.6f;
	// calc. some airMass given the engine displacement=1.839 and 4 cylinders (FORD_ESCORT_GT)
	fuelComputer->sdAirMassInOneCylinder =
			SpeedDensityBase::getAirmassImpl(/*VE*/ 1.0f, /*MAP*/ 100.0f, /*tChargeK*/ 273.15f + 20.0f);
	ASSERT_NEAR(0.5464f, fuelComputer->sdAirMassInOneCylinder, EPS4D);

	Sensor::setMockValue(SensorType::Clt, 90);
	Sensor::setMockValue(SensorType::Iat, 20);
	Sensor::setMockValue(SensorType::Map, 100);
	Sensor::setMockValue(SensorType::Tps1, 0);
	Sensor::setMockValue(SensorType::Rpm, 1000);

	// calc. airFlow using airMass, and find tCharge
	engine->periodicFastCallback();
	ASSERT_NEAR(59.12f, engine->engineState.sd.tCharge, EPS4D);
	ASSERT_NEAR(57 /*kg/h*/, engine->engineState.airflowEstimate, EPS4D);
}

TEST(misc, testIgnitionMapGenerator) {
	float rpmBin[16];
	setRpmBin(rpmBin, 16, 800, 7000);
	ASSERT_EQ(650, rpmBin[0]);
	ASSERT_EQ(800, rpmBin[1]) << "@1";
	ASSERT_EQ(1100, rpmBin[2]) << "@2";
	ASSERT_EQ(1400, rpmBin[3]) << "rpm@3";
	ASSERT_EQ(4700, rpmBin[14]) << "rpm@14";
	ASSERT_EQ(7000, rpmBin[15]);

	EXPECT_NEAR(36.0, getInitialAdvance(6000, 100, 36), 0.1);
	EXPECT_NEAR(9.9, getInitialAdvance(600, 100, 36), 0.2);

	EXPECT_NEAR(34.2, getInitialAdvance(2400, 40, 36), 0.1);
	EXPECT_NEAR(41.9, getInitialAdvance(4400, 40, 36), 0.2);
	EXPECT_NEAR(14.2, getInitialAdvance(800, 20, 36), 0.2);
}
