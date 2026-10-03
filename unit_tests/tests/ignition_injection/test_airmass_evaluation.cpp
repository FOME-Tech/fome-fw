#include "pch.h"

#include "alphan_airmass.h"
#include "fuel_math.h"
#include "maf_airmass.h"
#include "rusefi_lua.h"
#include "speed_density_airmass.h"

using ::testing::FloatNear;
using ::testing::InSequence;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrictMock;

namespace {
constexpr float AirGasConstant = 0.28705f;

mass_t expectedIdealGasMass(float displacement, int cylinders, float vePercent, float pressure, float temperatureK) {
	return (vePercent * 0.01f) * displacement * pressure / (AirGasConstant * temperatureK) / cylinders;
}

void seedPublishedDiagnostics() {
	engine->engineState.currentVe = 91;
	engine->engineState.veTableYAxis = 92;
	engine->engineState.idleVeTableYAxis = 93;
	engine->outputChannels.fallbackMap = 94;

	for (size_t i = 0; i < VE_BLEND_COUNT; i++) {
		engine->outputChannels.veBlendParameter[i] = 10 + i;
		engine->outputChannels.veBlendBias[i] = 20 + i;
		engine->outputChannels.veBlendOutput[i] = 30 + i;
		engine->outputChannels.veBlendYAxis[i] = 40 + i;
	}
}

void expectSeededDiagnostics() {
	EXPECT_FLOAT_EQ(engine->engineState.currentVe, 91);
	EXPECT_FLOAT_EQ(engine->engineState.veTableYAxis, 92);
	EXPECT_FLOAT_EQ(engine->engineState.idleVeTableYAxis, 93);
	EXPECT_FLOAT_EQ(static_cast<float>(engine->outputChannels.fallbackMap), 94);

	for (size_t i = 0; i < VE_BLEND_COUNT; i++) {
		EXPECT_FLOAT_EQ(static_cast<float>(engine->outputChannels.veBlendParameter[i]), 10 + i);
		EXPECT_FLOAT_EQ(static_cast<float>(engine->outputChannels.veBlendBias[i]), 20 + i);
		EXPECT_FLOAT_EQ(static_cast<float>(engine->outputChannels.veBlendOutput[i]), 30 + i);
		EXPECT_FLOAT_EQ(static_cast<float>(engine->outputChannels.veBlendYAxis[i]), 40 + i);
	}
}

void expectSameEvaluation(const AirmassEvaluation& left, const AirmassEvaluation& right) {
	EXPECT_FLOAT_EQ(left.Result.CylinderAirmass, right.Result.CylinderAirmass);
	EXPECT_FLOAT_EQ(left.Result.EngineLoadPercent, right.Result.EngineLoadPercent);
	EXPECT_EQ(left.Valid, right.Valid);
}

struct TestIdleController : public MockIdleController {
	bool isIdlingOrTaper() const override {
		return true;
	}
};

void configureFlatVeBlend(blend_table_s& blend, gppwm_channel_e parameter, gppwm_channel_e yAxis, float correction) {
	blend.blendParameter = parameter;
	blend.yAxisOverride = yAxis;
	setTable(blend.table, correction);
	setLinearCurve(blend.loadBins, 0, 100, 1);
	setLinearCurve(blend.rpmBins, 0, 7000, 1);
	setLinearCurve(blend.blendBins, 0, 100, 1);
	setArrayValues(blend.blendValues, 100);
}
} // namespace

TEST(AirmassEvaluation, LegacyModeIdsRemainStable) {
	EXPECT_EQ(static_cast<int>(LM_SPEED_DENSITY), 0);
	EXPECT_EQ(static_cast<int>(LM_REAL_MAF), 1);
	EXPECT_EQ(static_cast<int>(LM_ALPHA_N), 2);
	EXPECT_EQ(static_cast<int>(LM_LUA), 3);
	EXPECT_EQ(static_cast<int>(LM_MOCK), 100);
}

TEST(AirmassEvaluation, SpeedDensityPublicPostFlagControlsCoherentPublication) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engine->engineState.sd.tChargeK = 293.15f;
	engineConfiguration->displacement = 2.4f;
	setCylinderCount(4);
	engine->engineState.sd.tChargeK = 310;
	Sensor::setMockValue(SensorType::Tps1, 15);
	Sensor::setMockValue(SensorType::Map, 47);

	StrictMock<MockVp3d> veTable;
	StrictMock<MockVp3d> mapEstimate;
	{
		InSequence sequence;
		EXPECT_CALL(veTable, getValue(2500, 47)).WillOnce(Return(63));
		EXPECT_CALL(veTable, getValue(2500, 52)).WillOnce(Return(68));
	}

	SpeedDensityAirmass dut(&veTable, mapEstimate);
	seedPublishedDiagnostics();

	auto dryResult = dut.getAirmass(2500, false);
	EXPECT_NEAR(dryResult.CylinderAirmass, expectedIdealGasMass(2.4f, 4, 63, 47, 310), EPS4D);
	EXPECT_FLOAT_EQ(dryResult.EngineLoadPercent, 47);
	expectSeededDiagnostics();

	Sensor::setMockValue(SensorType::Map, 52);
	auto liveResult = dut.getAirmass(2500, true);
	EXPECT_NEAR(liveResult.CylinderAirmass, expectedIdealGasMass(2.4f, 4, 68, 52, 310), EPS4D);
	EXPECT_FLOAT_EQ(liveResult.EngineLoadPercent, 52);
	EXPECT_FLOAT_EQ(engine->engineState.currentVe, 68);
	EXPECT_FLOAT_EQ(engine->engineState.veTableYAxis, 52);
	EXPECT_FLOAT_EQ(engine->engineState.idleVeTableYAxis, 52);
	EXPECT_FLOAT_EQ(static_cast<float>(engine->outputChannels.fallbackMap), 94);
	for (size_t i = 0; i < VE_BLEND_COUNT; i++) {
		EXPECT_FLOAT_EQ(static_cast<float>(engine->outputChannels.veBlendOutput[i]), 0);
	}
}

TEST(AirmassEvaluation, AlphaNUsesTpsNativeLoadAndTemperature) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engine->engineState.sd.tChargeK = 293.15f;
	engineConfiguration->displacement = 3.2f;
	setCylinderCount(4);
	config->airmassTemperatureSource = AirmassTemperatureSource::Iat;
	Sensor::setMockValue(SensorType::Tps1, 12.5f);
	Sensor::setMockValue(SensorType::Iat, -3);

	StrictMock<MockVp3d> veTable;
	EXPECT_CALL(veTable, getValue(1800, FloatNear(12.5f, EPS4D))).WillOnce(Return(52));
	AlphaNAirmass dut(&veTable);

	auto result = dut.getAirmass(1800, true);
	EXPECT_NEAR(result.CylinderAirmass, expectedIdealGasMass(3.2f, 4, 52, 101.325f, 270.15f), EPS4D);
	EXPECT_FLOAT_EQ(result.EngineLoadPercent, 12.5f);
	EXPECT_FLOAT_EQ(engine->engineState.currentVe, 52);
	EXPECT_FLOAT_EQ(engine->engineState.veTableYAxis, 12.5f);
}

TEST(AirmassEvaluation, InvalidAlphaNTpsPreservesPublishedVeDiagnostics) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engine->engineState.sd.tChargeK = 293.15f;
	StrictMock<MockVp3d> veTable;
	AlphaNAirmass dut(&veTable);
	Sensor::resetMockValue(SensorType::Tps1);
	ASSERT_FALSE(Sensor::get(SensorType::Tps1).Valid);
	seedPublishedDiagnostics();

	AirmassDiagnostics diagnostics;
	diagnostics.Ve.HasValue = true;
	diagnostics.Ve.Valid = true;
	diagnostics.Map.HasValue = true;
	diagnostics.Map.Valid = true;
	auto evaluation = dut.evaluateAirmass(1900, &diagnostics);
	EXPECT_FALSE(evaluation.Valid);
	EXPECT_FALSE(diagnostics.Ve.HasValue);
	EXPECT_FALSE(diagnostics.Ve.Valid);
	EXPECT_FALSE(diagnostics.Map.HasValue);
	EXPECT_FALSE(diagnostics.Map.Valid);

	auto result = dut.getAirmass(1900, true);
	EXPECT_FLOAT_EQ(result.CylinderAirmass, 0);
	EXPECT_FLOAT_EQ(result.EngineLoadPercent, 100);
	expectSeededDiagnostics();
}

TEST(AirmassEvaluation, AlphaNCaptureMatchesNoCaptureAndResetsOnFailure) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engine->engineState.sd.tChargeK = 293.15f;
	engineConfiguration->displacement = 3.2f;
	setCylinderCount(4);
	Sensor::setMockValue(SensorType::Tps1, 18);

	StrictMock<MockVp3d> veTable;
	EXPECT_CALL(veTable, getValue(1900, 18)).Times(2).WillRepeatedly(Return(54));
	AlphaNAirmass dut(&veTable);
	seedPublishedDiagnostics();

	auto withoutCapture = dut.evaluateAirmass(1900);
	AirmassDiagnostics diagnostics;
	auto withCapture = dut.evaluateAirmass(1900, &diagnostics);
	expectSameEvaluation(withoutCapture, withCapture);
	EXPECT_TRUE(withCapture.Valid);
	EXPECT_TRUE(diagnostics.Ve.HasValue);
	EXPECT_TRUE(diagnostics.Ve.Valid);
	EXPECT_FLOAT_EQ(diagnostics.Ve.Ve, 54);
	EXPECT_FLOAT_EQ(diagnostics.Ve.Load, 18);
	EXPECT_FALSE(diagnostics.Map.HasValue);

	Sensor::setInvalidMockValue(SensorType::Tps1);
	auto failedWithoutCapture = dut.evaluateAirmass(1900);
	EXPECT_TRUE(diagnostics.Ve.HasValue);
	auto failedWithCapture = dut.evaluateAirmass(1900, &diagnostics);
	expectSameEvaluation(failedWithoutCapture, failedWithCapture);
	EXPECT_FALSE(failedWithCapture.Valid);
	EXPECT_FALSE(diagnostics.Ve.HasValue);
	EXPECT_FALSE(diagnostics.Ve.Valid);
	EXPECT_FALSE(diagnostics.Map.HasValue);
	EXPECT_FALSE(diagnostics.Map.Valid);
	expectSeededDiagnostics();
}

TEST(AirmassEvaluation, InvalidSpeedDensityTemperatureUsesIatThenStandardTemperature) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engine->engineState.sd.tChargeK = NAN;
	Sensor::setMockValue(SensorType::Tps1, 24);
	Sensor::setMockValue(SensorType::Map, 46);
	Sensor::setMockValue(SensorType::Iat, 30);
	StrictMock<MockVp3d> veTable;
	StrictMock<MockVp3d> mapEstimate;
	EXPECT_CALL(veTable, getValue(2100, 46)).Times(3).WillRepeatedly(Return(58));
	SpeedDensityAirmass dut(&veTable, mapEstimate);
	AirmassDiagnostics diagnostics;
	auto result = dut.evaluateAirmass(2100, &diagnostics);
	ASSERT_TRUE(result.Valid);
	EXPECT_TRUE(result.Degraded);
	EXPECT_FLOAT_EQ(diagnostics.TemperatureK, 303.15f);
	EXPECT_TRUE(diagnostics.TemperatureFallback);
	Sensor::setInvalidMockValue(SensorType::Iat);
	result = dut.evaluateAirmass(2100, &diagnostics);
	ASSERT_TRUE(result.Valid);
	EXPECT_TRUE(result.Degraded);
	EXPECT_FLOAT_EQ(diagnostics.TemperatureK, 293.15f);
	engine->engineState.sd.tChargeK = 310;
	result = dut.evaluateAirmass(2100, &diagnostics);
	ASSERT_TRUE(result.Valid);
	EXPECT_FALSE(result.Degraded);
	EXPECT_FALSE(diagnostics.TemperatureFallback);
	EXPECT_FLOAT_EQ(diagnostics.TemperatureK, 310);
}

TEST(AirmassEvaluation, SpeedDensityCaptureMatchesNoCaptureAndResetsOnEarlyFailure) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engine->engineState.sd.tChargeK = 293.15f;
	engineConfiguration->displacement = 2.4f;
	setCylinderCount(4);
	engine->engineState.sd.tChargeK = 310;
	Sensor::setMockValue(SensorType::Tps1, 24);
	Sensor::setMockValue(SensorType::Map, 46);

	StrictMock<MockVp3d> veTable;
	StrictMock<MockVp3d> mapEstimate;
	EXPECT_CALL(veTable, getValue(2100, 46)).Times(2).WillRepeatedly(Return(58));
	SpeedDensityAirmass dut(&veTable, mapEstimate);
	seedPublishedDiagnostics();

	auto withoutCapture = dut.evaluateAirmass(2100);
	AirmassDiagnostics diagnostics;
	auto withCapture = dut.evaluateAirmass(2100, &diagnostics);
	expectSameEvaluation(withoutCapture, withCapture);
	EXPECT_TRUE(withCapture.Valid);
	EXPECT_TRUE(diagnostics.Ve.HasValue);
	EXPECT_TRUE(diagnostics.Ve.Valid);
	EXPECT_FLOAT_EQ(diagnostics.Ve.Ve, 58);
	EXPECT_FLOAT_EQ(diagnostics.Ve.Load, 46);
	EXPECT_FALSE(diagnostics.Map.HasValue);
	EXPECT_TRUE(diagnostics.Map.Valid);
	EXPECT_FLOAT_EQ(diagnostics.Map.FallbackMap, 0);

	config->airmassTemperatureSource = static_cast<AirmassTemperatureSource>(255);
	auto failedWithoutCapture = dut.evaluateAirmass(2100);
	EXPECT_TRUE(diagnostics.Ve.HasValue);
	auto failedWithCapture = dut.evaluateAirmass(2100, &diagnostics);
	expectSameEvaluation(failedWithoutCapture, failedWithCapture);
	EXPECT_FALSE(failedWithCapture.Valid);
	EXPECT_FALSE(diagnostics.Ve.HasValue);
	EXPECT_FALSE(diagnostics.Ve.Valid);
	EXPECT_FALSE(diagnostics.Map.HasValue);
	EXPECT_TRUE(diagnostics.Map.Valid);
	EXPECT_FLOAT_EQ(diagnostics.Map.FallbackMap, 0);
	expectSeededDiagnostics();
}

TEST(AirmassEvaluation, MapSnapshotTracksTransientEstimateValidity) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	StrictMock<MockVp3d> veTable;
	StrictMock<MockVp3d> mapEstimate;
	SpeedDensityAirmass dut(&veTable, mapEstimate);
	config->useMapEstimateTable = true;
	engineConfiguration->useMapEstimateDuringTransient = true;
	engine->module<TpsAccelEnrichment>()->isAboveAccelThreshold = true;
	Sensor::setMockValue(SensorType::Map, 40);
	Sensor::setMockValue(SensorType::Tps1, 20);
	EXPECT_CALL(mapEstimate, getValue(3000, 20)).WillOnce(Return(75));
	auto validEstimate = dut.evaluateMap(3000);
	EXPECT_TRUE(validEstimate.Valid);
	EXPECT_TRUE(validEstimate.UsesEstimate);
	EXPECT_FLOAT_EQ(validEstimate.Map, 75);
	Sensor::setInvalidMockValue(SensorType::Tps1);
	EXPECT_CALL(mapEstimate, getValue(3000, 0)).WillOnce(Return(75));
	const auto missingTps = dut.evaluateMap(3000);
	EXPECT_TRUE(missingTps.Valid);
	EXPECT_TRUE(missingTps.Fallback);
	EXPECT_FLOAT_EQ(missingTps.Map, 75);
	EXPECT_CALL(mapEstimate, getValue(3000, 0)).WillOnce(Return(NAN));
	const auto brokenEstimate = dut.evaluateMap(3000);
	EXPECT_TRUE(brokenEstimate.Valid);
	EXPECT_TRUE(brokenEstimate.Fallback);
	EXPECT_FALSE(brokenEstimate.UsesEstimate);
	EXPECT_FLOAT_EQ(brokenEstimate.Map, 40);
	engineConfiguration->useMapEstimateDuringTransient = false;
	EXPECT_TRUE(dut.evaluateMap(3000).Valid);
}

TEST(AirmassEvaluation, ModelSnapshotsRemainIndependentUntilPublished) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engine->engineState.sd.tChargeK = 293.15f;
	engineConfiguration->displacement = 4.0f;
	setCylinderCount(4);
	engine->engineState.sd.tChargeK = 300;
	Sensor::setMockValue(SensorType::Tps1, 11);
	Sensor::setMockValue(SensorType::Map, 45);

	StrictMock<MockVp3d> sdVeTable;
	StrictMock<MockVp3d> alphaNVeTable;
	StrictMock<MockVp3d> mapEstimate;
	EXPECT_CALL(sdVeTable, getValue(2000, 45)).WillOnce(Return(60));
	EXPECT_CALL(alphaNVeTable, getValue(2000, 11)).WillOnce(Return(30));
	SpeedDensityAirmass sd(&sdVeTable, mapEstimate);
	AlphaNAirmass alphaN(&alphaNVeTable);
	seedPublishedDiagnostics();

	AirmassDiagnostics sdDiagnostics;
	AirmassDiagnostics alphaNDiagnostics;
	auto sdEvaluation = sd.evaluateAirmass(2000, &sdDiagnostics);
	auto alphaNEvaluation = alphaN.evaluateAirmass(2000, &alphaNDiagnostics);
	EXPECT_NEAR(sdEvaluation.Result.CylinderAirmass, expectedIdealGasMass(4, 4, 60, 45, 300), EPS4D);
	EXPECT_FLOAT_EQ(sdEvaluation.Result.EngineLoadPercent, 45);
	EXPECT_NEAR(alphaNEvaluation.Result.CylinderAirmass, expectedIdealGasMass(4, 4, 30, 101.325f, 300), EPS4D);
	EXPECT_FLOAT_EQ(alphaNEvaluation.Result.EngineLoadPercent, 11);
	EXPECT_FLOAT_EQ(sdDiagnostics.Ve.Ve, 60);
	EXPECT_FLOAT_EQ(alphaNDiagnostics.Ve.Ve, 30);
	expectSeededDiagnostics();

	// Publishing the retained SD value after evaluating Alpha-N must still publish the SD snapshot.
	AirmassVeModelBase::publishEvaluation(sdDiagnostics);
	EXPECT_FLOAT_EQ(engine->engineState.currentVe, 60);
	EXPECT_FLOAT_EQ(engine->engineState.veTableYAxis, 45);
	EXPECT_FLOAT_EQ(static_cast<float>(engine->outputChannels.fallbackMap), 94);
}

TEST(AirmassEvaluation, LuaModelQueriesAreDryReads) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engine->engineState.sd.tChargeK = 293.15f;
	engineConfiguration->displacement = 4.0f;
	setCylinderCount(4);
	engine->engineState.sd.tChargeK = 300;
	Sensor::setMockValue(SensorType::Rpm, 1200);
	Sensor::setMockValue(SensorType::Tps1, 20);
	Sensor::setMockValue(SensorType::Map, 50);
	Sensor::setMockValue(SensorType::Maf, 72);
	setTable(config->veTable, 50);
	setTable(config->alphaNTable, 50);
	setTable(config->mafTable, 50);
	setLinearCurve(config->veLoadBins, 0, 100, 1);
	setLinearCurve(config->veRpmBins, 0, 7000, 1);
	setTable(config->mapEstimateTable, 70);
	setLinearCurve(config->mapEstimateTpsBins, 0, 100, 1);
	setLinearCurve(config->mapEstimateRpmBins, 0, 7000, 1);
	initFuelMap();
	seedPublishedDiagnostics();

	EXPECT_NEAR(
			testLuaReturnsNumber("function testFunc() return getAirmass(0) end"),
			expectedIdealGasMass(4, 4, 50, 50, 300),
			EPS4D);
	EXPECT_NEAR(
			testLuaReturnsNumber("function testFunc() return getAirmass(2) end"),
			expectedIdealGasMass(4, 4, 50, 101.325f, 300),
			EPS4D);
	// 72 kg/h = 20 g/s; at 1200 rpm and four cylinders this is 0.5 g/cylinder before the 50% table.
	EXPECT_NEAR(testLuaReturnsNumber("function testFunc() return getAirmass(1) end"), 0.25f, EPS4D);
	expectSeededDiagnostics();
}

TEST(AirmassEvaluation, IdleTaperThenCompoundsCorrectionsAndPublishesCapturedAxes) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engine->engineState.sd.tChargeK = 293.15f;
	StrictMock<MockVp3d> veTable;
	EXPECT_CALL(veTable, getValue(2300, 35)).WillOnce(Return(60));

	TestIdleController idle;
	engine->engineModules.get<IdleController>().set(&idle);
	engineConfiguration->useSeparateVeForIdle = true;
	config->idleVeLoadSource = IdleVeLoadSource::Tps;
	engineConfiguration->idlePidDeactivationTpsThreshold = 10;
	setTable(config->idleVeTable, 40);
	setLinearCurve(config->idleVeLoadBins, 0, 100, 1);
	setLinearCurve(config->idleVeRpmBins, 0, 2500, 1);

	configureFlatVeBlend(config->veBlends[0], GPPWM_Clt, GPPWM_Zero, 20);
	configureFlatVeBlend(config->veBlends[1], GPPWM_Iat, GPPWM_Map, -25);
	Sensor::setMockValue(SensorType::DriverThrottleIntent, 7.5f);
	Sensor::setMockValue(SensorType::Tps1, 22);
	Sensor::setMockValue(SensorType::Map, 83);
	Sensor::setMockValue(SensorType::Clt, 44);
	Sensor::setMockValue(SensorType::Iat, 33);

	AlphaNAirmass dut(&veTable);
	seedPublishedDiagnostics();
	VeDiagnostics diagnostics;
	auto evaluation = dut.evaluateVe(2300, 35, &diagnostics);

	// Idle taper first: 40 + 50% * (60 - 40) = 50. Then 50 * 1.20 * 0.75 = 45.
	EXPECT_TRUE(evaluation.Valid);
	EXPECT_FLOAT_EQ(evaluation.Ve, 45);
	EXPECT_TRUE(diagnostics.Valid);
	EXPECT_TRUE(diagnostics.HasValue);
	EXPECT_FLOAT_EQ(diagnostics.Ve, 45);
	EXPECT_FLOAT_EQ(diagnostics.Load, 35);
	EXPECT_FLOAT_EQ(diagnostics.IdleLoad, 22);
	EXPECT_FLOAT_EQ(diagnostics.Blends[0].BlendParameter, 44);
	EXPECT_FLOAT_EQ(diagnostics.Blends[0].Value, 20);
	EXPECT_FLOAT_EQ(diagnostics.Blends[0].TableYAxis, 35);
	EXPECT_FLOAT_EQ(diagnostics.Blends[1].BlendParameter, 33);
	EXPECT_FLOAT_EQ(diagnostics.Blends[1].Value, -25);
	EXPECT_FLOAT_EQ(diagnostics.Blends[1].TableYAxis, 83);
	expectSeededDiagnostics();

	// Publication must consume the captured values, without looking up changed sensors or the table again.
	Sensor::setMockValue(SensorType::Tps1, 2);
	Sensor::setMockValue(SensorType::Map, 9);
	Sensor::setMockValue(SensorType::Clt, 1);
	Sensor::setMockValue(SensorType::Iat, 2);
	AirmassVeModelBase::publishVe(diagnostics);
	EXPECT_FLOAT_EQ(engine->engineState.currentVe, 45);
	EXPECT_FLOAT_EQ(engine->engineState.veTableYAxis, 35);
	EXPECT_FLOAT_EQ(engine->engineState.idleVeTableYAxis, 22);
	EXPECT_FLOAT_EQ(static_cast<float>(engine->outputChannels.veBlendParameter[0]), 44);
	EXPECT_FLOAT_EQ(static_cast<float>(engine->outputChannels.veBlendOutput[0]), 20);
	EXPECT_FLOAT_EQ(static_cast<float>(engine->outputChannels.veBlendYAxis[0]), 35);
	EXPECT_FLOAT_EQ(static_cast<float>(engine->outputChannels.veBlendParameter[1]), 33);
	EXPECT_FLOAT_EQ(static_cast<float>(engine->outputChannels.veBlendOutput[1]), -25);
	EXPECT_FLOAT_EQ(static_cast<float>(engine->outputChannels.veBlendYAxis[1]), 83);
}

TEST(AirmassEvaluation, DualMafUsesSumAndSingleBankFallbacks) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engine->engineState.sd.tChargeK = 293.15f;
	engineConfiguration->displacement = 2.0f;
	setCylinderCount(4);
	Sensor::setMockValue(SensorType::Maf, 40);
	Sensor::setMockValue(SensorType::Maf2, 60);

	NiceMock<MockVp3d> veTable;
	ON_CALL(veTable, getValue(6000, testing::_)).WillByDefault(Return(100));
	MafAirmass dut(&veTable);

	constexpr float massPerKgPerHour = 1.0f / 720.0f;
	const float standardCharge = (2.0f / 4) * 101.325f / (AirGasConstant * 293.15f);
	auto expectFlow = [&](float effectiveFlow, const AirmassResult& result) {
		const float expectedMass = effectiveFlow * massPerKgPerHour;
		EXPECT_NEAR(result.CylinderAirmass, expectedMass, EPS4D);
		EXPECT_NEAR(result.EngineLoadPercent, 100 * expectedMass / standardCharge, EPS4D);
	};

	expectFlow(100, dut.getAirmass(6000, false));

	Sensor::setInvalidMockValue(SensorType::Maf);
	expectFlow(120, dut.getAirmass(6000, false));

	Sensor::setMockValue(SensorType::Maf, 40);
	Sensor::setInvalidMockValue(SensorType::Maf2);
	expectFlow(80, dut.getAirmass(6000, false));

	// A real zero-flow sample is usable, while an invalid sensor also returns zero but is not usable.
	Sensor::setMockValue(SensorType::Maf, 0);
	auto validZero = dut.evaluateAirmass(6000);
	EXPECT_TRUE(validZero.Valid);
	EXPECT_FLOAT_EQ(validZero.Result.CylinderAirmass, 0);

	Sensor::setInvalidMockValue(SensorType::Maf);
	auto invalid = dut.evaluateAirmass(6000);
	EXPECT_FALSE(invalid.Valid);
	EXPECT_FLOAT_EQ(invalid.Result.CylinderAirmass, 0);
}

TEST(AirmassEvaluation, MafCaptureMatchesNoCaptureAndRpmZeroClearsReuse) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engine->engineState.sd.tChargeK = 293.15f;
	engineConfiguration->displacement = 2.0f;
	setCylinderCount(4);
	Sensor::setMockValue(SensorType::Maf, 72);
	Sensor::resetMockValue(SensorType::Maf2);

	NiceMock<MockVp3d> veTable;
	ON_CALL(veTable, getValue(6000, testing::_)).WillByDefault(Return(80));
	MafAirmass dut(&veTable);
	seedPublishedDiagnostics();

	auto withoutCapture = dut.evaluateAirmass(6000);
	AirmassDiagnostics diagnostics;
	auto withCapture = dut.evaluateAirmass(6000, &diagnostics);
	expectSameEvaluation(withoutCapture, withCapture);
	EXPECT_TRUE(withCapture.Valid);
	EXPECT_TRUE(diagnostics.Ve.HasValue);
	EXPECT_TRUE(diagnostics.Ve.Valid);
	EXPECT_FLOAT_EQ(diagnostics.Ve.Ve, 80);

	// A failed MAF cannot publish a valid model or consumer snapshot.
	Sensor::setInvalidMockValue(SensorType::Maf);
	auto invalidSensorWithoutCapture = dut.evaluateAirmass(6000);
	auto invalidSensorWithCapture = dut.evaluateAirmass(6000, &diagnostics);
	expectSameEvaluation(invalidSensorWithoutCapture, invalidSensorWithCapture);
	EXPECT_FALSE(invalidSensorWithCapture.Valid);
	EXPECT_FALSE(diagnostics.Ve.HasValue);
	EXPECT_FALSE(diagnostics.Ve.Valid);

	// RPM zero exits before VE evaluation and must clear presence on a reused capture.
	auto failedWithoutCapture = dut.evaluateAirmass(0);
	EXPECT_FALSE(diagnostics.Ve.HasValue);
	auto failedWithCapture = dut.evaluateAirmass(0, &diagnostics);
	expectSameEvaluation(failedWithoutCapture, failedWithCapture);
	EXPECT_FALSE(failedWithCapture.Valid);
	EXPECT_FALSE(diagnostics.Ve.HasValue);
	EXPECT_FALSE(diagnostics.Ve.Valid);
	EXPECT_FALSE(diagnostics.Map.HasValue);
	EXPECT_FALSE(diagnostics.Map.Valid);
	expectSeededDiagnostics();
}

TEST(AirmassEvaluation, MafRejectsNonfiniteInputsBeforeInterpolationOrPublication) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	StrictMock<MockVp3d> veTable;
	MafAirmass maf(&veTable);
	seedPublishedDiagnostics();
	for (float invalid : {NAN, INFINITY, -1.0f}) {
		EXPECT_FALSE(maf.evaluateAirmassImpl(invalid, 2000).Valid);
		EXPECT_FALSE(maf.evaluateAirmassImpl(72, invalid).Valid);
		Sensor::setMockValue(SensorType::Maf, invalid);
		Sensor::setInvalidMockValue(SensorType::Maf2);
		const auto result = maf.getAirmassForFuel(2000);
		EXPECT_FALSE(result.Valid);
		EXPECT_FLOAT_EQ(result.Result.CylinderAirmass, 0);
		expectSeededDiagnostics();
	}
}
