#include "pch.h"

#include "alphan_airmass.h"
#include "fuel_math.h"
#include "maf_airmass.h"

namespace {
constexpr float AirGasConstant = 0.28705f;

mass_t
expectedIdealGasMass(float displacement, int cylinders, float fillingPercent, float pressure, float temperatureK) {
	return (fillingPercent * 0.01f) * displacement * pressure / (AirGasConstant * temperatureK) / cylinders;
}

struct DedicatedTableIdleController : public MockIdleController {
	bool isIdlingOrTaper() const override {
		return true;
	}
};

void seedPublishedVeDiagnostics() {
	engine->engineState.currentVe = 91;
	engine->engineState.veTableYAxis = 92;
	engine->engineState.idleVeTableYAxis = 93;
}

void expectPublishedVeDiagnosticsUnchanged() {
	EXPECT_FLOAT_EQ(engine->engineState.currentVe, 91);
	EXPECT_FLOAT_EQ(engine->engineState.veTableYAxis, 92);
	EXPECT_FLOAT_EQ(engine->engineState.idleVeTableYAxis, 93);
}

void configureAlphaNAxesForInterpolation() {
	setLinearCurve(config->alphaNTpsBins, 0, 7.5f, 0.1f);
	setLinearCurve(config->alphaNRpmBins, 500, 8000, 1);
	setTable(config->alphaNTable, 10);

	// At TPS 1.25 and 1750 rpm all four interpolation weights are 0.5.
	config->alphaNTable[2][2] = 40;
	config->alphaNTable[3][2] = 60;
	config->alphaNTable[2][3] = 80;
	config->alphaNTable[3][3] = 100;
	config->alphaNTable[0][0] = 33;
	config->alphaNTable[ALPHA_N_LOAD_COUNT - 1][ALPHA_N_RPM_COUNT - 1] = 99;
}

void configureMafAxesForInterpolation() {
	setLinearCurve(config->mafLoadBins, 0, 150, 1);
	setLinearCurve(config->mafRpmBins, 500, 8000, 1);
	setTable(config->mafTable, 10);

	// At 55% filling and 1750 rpm all four interpolation weights are 0.5.
	config->mafTable[5][2] = 40;
	config->mafTable[6][2] = 60;
	config->mafTable[5][3] = 80;
	config->mafTable[6][3] = 100;
}
} // namespace

TEST(DedicatedAirmassTables, M73AlphaNCalibrationUsesItsDedicatedTable) {
	EngineTestHelper eth(engine_type_e::FRANKENSO_BMW_M73_F);
	engine->engineState.sd.tChargeK = 293.15f;
	Sensor::setMockValue(SensorType::Tps1, 25);

	EXPECT_EQ(ALPHA_N_LOAD_COUNT, 16);
	EXPECT_EQ(ALPHA_N_RPM_COUNT, 16);
	EXPECT_EQ(MAF_LOAD_COUNT, 16);
	EXPECT_EQ(MAF_RPM_COUNT, 16);
	EXPECT_FLOAT_EQ(config->alphaNTable[0][0], 45);
	EXPECT_FLOAT_EQ(config->alphaNTpsBins[1], 0.5f);
	EXPECT_FLOAT_EQ(config->mafLoadBins[MAF_LOAD_COUNT - 1], 200);
	for (size_t load = 0; load < ALPHA_N_LOAD_COUNT; load++) {
		for (size_t rpm = 0; rpm < ALPHA_N_RPM_COUNT; rpm++) {
			EXPECT_FLOAT_EQ(config->alphaNTable[load][rpm], 45);
		}
	}
	for (size_t load = 0; load < MAF_LOAD_COUNT; load++) {
		for (size_t rpm = 0; rpm < MAF_RPM_COUNT; rpm++) {
			EXPECT_FLOAT_EQ(config->mafTable[load][rpm], 100);
		}
	}

	// EngineTestHelper selects LM_MOCK after the preset callback, so restore the
	// preset's algorithm before asking the model dispatcher for the active model.
	engineConfiguration->fuelAlgorithm = LM_ALPHA_N;
	const auto airmass = getAirmassModel(engineConfiguration->fuelAlgorithm)->getAirmass(1800, true);

	EXPECT_FLOAT_EQ(engine->engineState.currentVe, 45);
	EXPECT_FLOAT_EQ(airmass.EngineLoadPercent, 25);
	EXPECT_NEAR(
			airmass.CylinderAirmass,
			expectedIdealGasMass(
					engineConfiguration->displacement, engine->engineState.cylinderCount, 45, 101.325f, 293.15f),
			EPS4D);
}

TEST(DedicatedAirmassTables, AlphaNUsesFractionalTpsAndIndependentRpmAxes) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engine->engineState.sd.tChargeK = 293.15f;
	engineConfiguration->fuelAlgorithm = LM_REAL_MAF;
	engineConfiguration->displacement = 3.2f;
	setCylinderCount(4);
	configureAlphaNAxesForInterpolation();

	AlphaNAirmass alphaN;
	AirmassDiagnostics diagnostics;
	Sensor::setMockValue(SensorType::Tps1, 1.25f);
	auto midpoint = alphaN.evaluateAirmass(1750, &diagnostics);
	EXPECT_TRUE(midpoint.Valid);
	EXPECT_FLOAT_EQ(midpoint.Result.EngineLoadPercent, 1.25f);
	EXPECT_FLOAT_EQ(diagnostics.Ve.Ve, 70);
	EXPECT_FLOAT_EQ(diagnostics.Ve.Load, 1.25f);
	EXPECT_NEAR(midpoint.Result.CylinderAirmass, expectedIdealGasMass(3.2f, 4, 70, 101.325f, 293.15f), EPS4D);

	Sensor::setMockValue(SensorType::Tps1, 1.1f);
	auto asymmetric = alphaN.evaluateAirmass(1875, &diagnostics);
	EXPECT_TRUE(asymmetric.Valid);
	EXPECT_NEAR(asymmetric.Result.EngineLoadPercent, 1.1f, EPS4D);
	EXPECT_NEAR(diagnostics.Ve.Ve, 74, EPS4D);
	EXPECT_NEAR(diagnostics.Ve.Load, 1.1f, EPS4D);
	EXPECT_NEAR(asymmetric.Result.CylinderAirmass, expectedIdealGasMass(3.2f, 4, 74, 101.325f, 293.15f), EPS4D);

	Sensor::setMockValue(SensorType::Tps1, 0);
	auto belowAxes = alphaN.evaluateAirmass(100, &diagnostics);
	EXPECT_TRUE(belowAxes.Valid);
	EXPECT_FLOAT_EQ(diagnostics.Ve.Ve, 33);
	EXPECT_NEAR(belowAxes.Result.CylinderAirmass, expectedIdealGasMass(3.2f, 4, 33, 101.325f, 293.15f), EPS4D);

	Sensor::setMockValue(SensorType::Tps1, 100);
	auto aboveAxes = alphaN.evaluateAirmass(9000, &diagnostics);
	EXPECT_TRUE(aboveAxes.Valid);
	EXPECT_FLOAT_EQ(diagnostics.Ve.Ve, 99);
	EXPECT_NEAR(aboveAxes.Result.CylinderAirmass, expectedIdealGasMass(3.2f, 4, 99, 101.325f, 293.15f), EPS4D);
}

TEST(DedicatedAirmassTables, MafUsesNativeFillingAndIndependentRpmAxes) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engine->engineState.sd.tChargeK = 293.15f;
	engineConfiguration->fuelAlgorithm = LM_ALPHA_N;
	engineConfiguration->displacement = 2.0f;
	setCylinderCount(4);
	configureMafAxesForInterpolation();

	constexpr float rpm = 1750;
	constexpr float nativeLoad = 55;
	const float rawCylinderMass = nativeLoad * 0.01f * getStandardAirCharge();
	const float mafKgPerHour = rawCylinderMass * rpm * engine->engineState.cylinderCount * 0.03f;

	MafAirmass maf;
	AirmassDiagnostics diagnostics;
	auto evaluation = maf.evaluateAirmassImpl(mafKgPerHour, rpm, &diagnostics);

	EXPECT_TRUE(evaluation.Valid);
	EXPECT_NEAR(evaluation.Result.EngineLoadPercent, nativeLoad, EPS4D);
	EXPECT_NEAR(diagnostics.Ve.Load, nativeLoad, EPS4D);
	EXPECT_FLOAT_EQ(diagnostics.Ve.Ve, 70);
	EXPECT_NEAR(evaluation.Result.CylinderAirmass, rawCylinderMass * 0.70f, EPS4D);
}

TEST(DedicatedAirmassTables, MainMapOwnershipAndAxesDoNotFollowGlobalMode) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engine->engineState.sd.tChargeK = 300;
	setTable(config->veTable, 60);
	setTable(config->alphaNTable, 25);
	Sensor::setMockValue(SensorType::Tps1, 12);
	Sensor::setMockValue(SensorType::Map, 75);
	AlphaNAirmass alphaN;
	for (auto mode : {LM_SPEED_DENSITY, LM_ALPHA_N, LM_REAL_MAF, LM_SD_ALPHA_N}) {
		engineConfiguration->fuelAlgorithm = mode;
		AirmassDiagnostics diagnostics;
		const auto evaluation = alphaN.evaluateAirmass(2200, &diagnostics);
		EXPECT_TRUE(evaluation.Valid);
		EXPECT_FLOAT_EQ(diagnostics.Ve.Load, 12);
		EXPECT_FLOAT_EQ(diagnostics.Ve.Ve, 25);
	}
}

TEST(DedicatedAirmassTables, ConfigurationChangeRejectsInvalidActiveConfiguration) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->fuelAlgorithm = LM_ALPHA_N;
	const auto version = engine->globalConfigurationVersion;
	config->alphaNRpmBins[ALPHA_N_RPM_COUNT - 1] = 18001;
	EXPECT_FATAL_ERROR(incrementGlobalConfigurationVersion());
	EXPECT_EQ(engine->globalConfigurationVersion, version);
}

TEST(DedicatedAirmassTables, DedicatedAxesMustBeStrictlyAscending) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	ASSERT_TRUE(isAirmassModelConfigurationValid(LM_ALPHA_N));
	ASSERT_TRUE(isAirmassModelConfigurationValid(LM_REAL_MAF));
	config->alphaNTpsBins[2] = config->alphaNTpsBins[1];
	EXPECT_FALSE(isAirmassModelConfigurationValid(LM_ALPHA_N));
	EXPECT_TRUE(isAirmassModelConfigurationValid(LM_REAL_MAF));
	setLinearCurve(config->alphaNTpsBins, 0, 100, 1);
	config->alphaNRpmBins[ALPHA_N_RPM_COUNT - 1] = 18001;
	EXPECT_FALSE(isAirmassModelConfigurationValid(LM_ALPHA_N));
	config->mafLoadBins[2] = config->mafLoadBins[1];
	EXPECT_FALSE(isAirmassModelConfigurationValid(LM_REAL_MAF));
	setLinearCurve(config->mafLoadBins, 0, 200, 1);
	config->mafRpmBins[MAF_RPM_COUNT - 1] = 18001;
	EXPECT_FALSE(isAirmassModelConfigurationValid(LM_REAL_MAF));
}

TEST(DedicatedAirmassTables, InvalidAxisDryReadDoesNotPublishOrRaiseFault) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engine->engineState.sd.tChargeK = 293.15f;
	engineConfiguration->fuelAlgorithm = LM_ALPHA_N;
	config->alphaNTpsBins[2] = config->alphaNTpsBins[1];
	Sensor::setMockValue(SensorType::Tps1, 12);
	seedPublishedVeDiagnostics();
	const int warningCount = eth.getWarningCounter();

	AlphaNAirmass alphaN;
	AirmassDiagnostics diagnostics;
	AirmassEvaluation evaluation;
	EXPECT_NO_FATAL_ERROR(evaluation = alphaN.evaluateAirmass(2200, &diagnostics));
	EXPECT_FALSE(evaluation.Valid);
	EXPECT_FLOAT_EQ(evaluation.Result.CylinderAirmass, 0);
	EXPECT_FLOAT_EQ(evaluation.Result.EngineLoadPercent, 12);
	EXPECT_FALSE(diagnostics.Ve.HasValue);
	EXPECT_FALSE(diagnostics.Ve.Valid);
	EXPECT_EQ(eth.getWarningCounter(), warningCount);
	EXPECT_FALSE(hasFirmwareError());
	expectPublishedVeDiagnosticsUnchanged();
}

TEST(DedicatedAirmassTables, UnusedModelAxesDoNotBlockTheActiveModel) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engine->engineState.sd.tChargeK = 293.15f;
	engineConfiguration->fuelAlgorithm = LM_ALPHA_N;
	config->mafLoadBins[MAF_LOAD_COUNT - 1] = 1001;

	Sensor::setMockValue(SensorType::Tps1, 20);
	AlphaNAirmass alphaN;
	EXPECT_TRUE(alphaN.evaluateAirmass(2200).Valid);
}

TEST(DedicatedAirmassTables, DedicatedMainAxisRetainsIdleOverride) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engine->engineState.sd.tChargeK = 293.15f;
	engineConfiguration->veOverrideMode = VE_None;
	engineConfiguration->useSeparateVeForIdle = true;
	config->idleVeLoadSource = IdleVeLoadSource::MeasuredMap;
	engineConfiguration->idlePidDeactivationTpsThreshold = 10;
	engineConfiguration->displacement = 2.4f;
	setCylinderCount(4);
	setTable(config->alphaNTable, 80);
	setTable(config->idleVeTable, 40);
	setLinearCurve(config->idleVeLoadBins, 0, 100, 1);
	setLinearCurve(config->idleVeRpmBins, 0, 2500, 1);

	DedicatedTableIdleController idle;
	engine->engineModules.get<IdleController>().set(&idle);
	Sensor::setMockValue(SensorType::Tps1, 12.5f);
	Sensor::setMockValue(SensorType::Map, 77);
	Sensor::setMockValue(SensorType::DriverThrottleIntent, 7.5f);

	AlphaNAirmass alphaN;
	AirmassDiagnostics diagnostics;
	auto evaluation = alphaN.evaluateAirmass(2300, &diagnostics);

	// The dedicated TPS-axis value is 80. Idle MAP override produces 40, and taper is halfway between them.
	EXPECT_TRUE(evaluation.Valid);
	EXPECT_FLOAT_EQ(evaluation.Result.EngineLoadPercent, 12.5f);
	EXPECT_FLOAT_EQ(diagnostics.Ve.Load, 12.5f);
	EXPECT_FLOAT_EQ(diagnostics.Ve.IdleLoad, 77);
	EXPECT_FLOAT_EQ(diagnostics.Ve.Ve, 60);
	EXPECT_NEAR(evaluation.Result.CylinderAirmass, expectedIdealGasMass(2.4f, 4, 60, 101.325f, 293.15f), EPS4D);
}

TEST(DedicatedAirmassTables, MafIdleRetainsItsCorrectionMeaningAndIgnoresIdealGasOptions) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->fuelAlgorithm = LM_REAL_MAF;
	engineConfiguration->displacement = 2;
	setCylinderCount(4);
	engineConfiguration->useSeparateVeForIdle = true;
	engineConfiguration->idlePidDeactivationTpsThreshold = 10;
	config->idleVeModel = IdleVeModel::AlphaN;
	config->alphaNMultiplyMap = true;
	config->alphaNBaroCompensation = true;
	config->airmassTemperatureSource = AirmassTemperatureSource::Iat;
	Sensor::setInvalidMockValue(SensorType::Iat);
	Sensor::setInvalidMockValue(SensorType::BarometricPressure);
	Sensor::setMockValue(SensorType::Tps1, 20);
	Sensor::setMockValue(SensorType::Map, 55);
	Sensor::setMockValue(SensorType::DriverThrottleIntent, 0);
	setTable(config->mafTable, 100);
	setTable(config->idleVeTable, 50);
	setLinearCurve(config->idleVeLoadBins, 0, 100, 1);
	setLinearCurve(config->idleVeRpmBins, 0, 2500, 1);
	DedicatedTableIdleController idle;
	engine->engineModules.get<IdleController>().set(&idle);
	MafAirmass maf;
	for (auto axis :
		 {IdleVeLoadSource::ModelDefault,
		  IdleVeLoadSource::Tps,
		  IdleVeLoadSource::MeasuredMap,
		  IdleVeLoadSource::EffectiveMap}) {
		config->idleVeLoadSource = axis;
		AirmassDiagnostics diagnostics;
		const auto evaluation = maf.evaluateAirmassImpl(72, 2000, &diagnostics);
		ASSERT_TRUE(evaluation.Valid);
		EXPECT_NEAR(evaluation.Result.CylinderAirmass, 0.15f, EPS4D);
		EXPECT_FLOAT_EQ(diagnostics.Ve.Ve, 50);
		EXPECT_FLOAT_EQ(
				diagnostics.Ve.IdleLoad,
				axis == IdleVeLoadSource::ModelDefault ? evaluation.Result.EngineLoadPercent
				: axis == IdleVeLoadSource::Tps		   ? 20
													   : 55);
	}
}
