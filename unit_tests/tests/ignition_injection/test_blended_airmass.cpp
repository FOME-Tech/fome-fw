#include "pch.h"

#include "alphan_airmass.h"
#include "blended_airmass.h"
#include "fuel_math.h"
#include "speed_density_airmass.h"
#include "tunerstudio.h"

using ::testing::Return;
using ::testing::StrictMock;

namespace {
constexpr float AirGasConstant = 0.28705f;

mass_t expectedIdealGasMass(float displacement, int cylinders, float vePercent, float pressure, float temperatureK) {
	return (vePercent * 0.01f) * displacement * pressure / (AirGasConstant * temperatureK) / cylinders;
}

void configureFlatVeBlend(blend_table_s& blend, gppwm_channel_e parameter, gppwm_channel_e yAxis, float correction) {
	blend.blendParameter = parameter;
	blend.yAxisOverride = yAxis;
	setTable(blend.table, correction);
	setLinearCurve(blend.loadBins, 0, 100, 1);
	setLinearCurve(blend.rpmBins, 0, 7000, 1);
	setLinearCurve(blend.blendBins, 0, 100, 1);
	setArrayValues(blend.blendValues, 100);
}

class BlendedAirmassTest : public ::testing::Test {
protected:
	BlendedAirmassTest()
		: eth(engine_type_e::TEST_ENGINE) {
		engineConfiguration->fuelAlgorithm = LM_SD_ALPHA_N;
		engineConfiguration->useSeparateVeForIdle = false;
		engineConfiguration->displacement = 4;
		setCylinderCount(4);
		engine->engineState.sd.tChargeK = 300;
		setTable(config->veTable, 60);
		setTable(config->alphaNTable, 40);
		Sensor::setMockValue(SensorType::Map, 50);
		Sensor::setMockValue(SensorType::Tps1, 20);
		Sensor::setMockValue(SensorType::AcceleratorPedal, 30);
		Sensor::setMockValue(SensorType::Iat, 20);
	}

	EngineTestHelper eth;
};
} // namespace

TEST_F(BlendedAirmassTest, AnalyticalAuthorityWeightsUseEachRawModelOnce) {
	StrictMock<MockVp3d> mapEstimate;
	SpeedDensityAirmass sd(nullptr, mapEstimate);
	AlphaNAirmass alphaN;
	BlendedAirmass blended(sd, alphaN);
	const float sdMass = expectedIdealGasMass(4, 4, 60, 50, 300);
	const float alphaNMass = expectedIdealGasMass(4, 4, 40, 101.325f, 300);

	for (float authority : {0, 25, 50, 75, 100}) {
		setTable(config->airmassBlendTable, authority);
		BlendedAirmassDiagnostics diagnostics;
		const auto result = blended.evaluateAirmass(2500, &diagnostics);
		const float weight = authority * 0.01f;
		const float expectedMass = authority == 0	? sdMass
								 : authority == 100 ? alphaNMass
													: (1 - weight) * sdMass + weight * alphaNMass;

		ASSERT_TRUE(result.Airmass.Valid) << authority;
		EXPECT_EQ(result.Fault, AirmassInjectionFault::None);
		EXPECT_NEAR(result.Airmass.Result.CylinderAirmass, expectedMass, EPS4D);
		EXPECT_FLOAT_EQ(result.Airmass.Result.EngineLoadPercent, 50);
		EXPECT_NEAR(result.NormalizedFilling, 100 * expectedMass / getStandardAirCharge(), EPS4D);
		EXPECT_FLOAT_EQ(result.LambdaLoad.Value, 50);
		EXPECT_EQ(result.LambdaLoad.Source, AirmassLoadSource::EffectiveMap);
		EXPECT_FLOAT_EQ(result.IgnitionLoad.Value, 50);
		EXPECT_EQ(result.IgnitionLoad.Source, AirmassLoadSource::EffectiveMap);
		EXPECT_FLOAT_EQ(diagnostics.RequestedAuthority, authority);
		EXPECT_FLOAT_EQ(diagnostics.EffectiveAuthority, authority);
		EXPECT_TRUE(diagnostics.Corrections.HasValue);
		EXPECT_TRUE(diagnostics.Corrections.Valid);
		EXPECT_TRUE(diagnostics.Flags & BlendedCalculationValid);

		EXPECT_EQ((diagnostics.Flags & BlendedSdEvaluated) != 0, authority < 100);
		EXPECT_EQ((diagnostics.Flags & BlendedAlphaNEvaluated) != 0, authority > 0);
		EXPECT_EQ((diagnostics.Flags & BlendedSdValid) != 0, authority < 100);
		EXPECT_EQ((diagnostics.Flags & BlendedAlphaNValid) != 0, authority > 0);
		EXPECT_FALSE(diagnostics.Flags & BlendedEstimateEvaluated);
		EXPECT_FALSE(diagnostics.Flags & BlendedMapEstimateUsed);
		if (authority < 100) {
			EXPECT_NEAR(diagnostics.SdMass, sdMass, EPS4D);
			EXPECT_FLOAT_EQ(diagnostics.Sd.TableValue, 60);
		}
		if (authority > 0) {
			EXPECT_NEAR(diagnostics.AlphaNMass, alphaNMass, EPS4D);
			EXPECT_FLOAT_EQ(diagnostics.AlphaN.TableValue, 40);
		}
	}
}

TEST_F(BlendedAirmassTest, AuthorityMapUsesTpsRowsAndRpmColumnsWithClampedEdges) {
	setLinearCurve(config->airmassBlendTpsBins, 0, 70, 0.1f);
	setLinearCurve(config->airmassBlendRpmBins, 1000, 8000, 1);
	setTable(config->airmassBlendTable, 0);
	config->airmassBlendTable[2][2] = 10;
	config->airmassBlendTable[3][2] = 30;
	config->airmassBlendTable[2][3] = 50;
	config->airmassBlendTable[3][3] = 70;
	config->airmassBlendTable[0][0] = 33;
	config->airmassBlendTable[AIRMASS_BLEND_LOAD_COUNT - 1][AIRMASS_BLEND_RPM_COUNT - 1] = 99;
	StrictMock<MockVp3d> mapEstimate;
	SpeedDensityAirmass sd(nullptr, mapEstimate);
	AlphaNAirmass alphaN;
	BlendedAirmass blended(sd, alphaN);

	Sensor::setMockValue(SensorType::Tps1, 22);
	BlendedAirmassDiagnostics diagnostics;
	const auto asymmetric = blended.evaluateAirmass(3750, &diagnostics);
	ASSERT_TRUE(asymmetric.Airmass.Valid);
	EXPECT_NEAR(diagnostics.RequestedAuthority, 44, EPS4D);
	const float sdMass = expectedIdealGasMass(4, 4, 60, 50, 300);
	const float alphaNMass = expectedIdealGasMass(4, 4, 40, 101.325f, 300);
	EXPECT_NEAR(asymmetric.Airmass.Result.CylinderAirmass, 0.56f * sdMass + 0.44f * alphaNMass, EPS4D);

	Sensor::setMockValue(SensorType::Tps1, 0);
	EXPECT_TRUE(blended.evaluateAirmass(100, &diagnostics).Airmass.Valid);
	EXPECT_FLOAT_EQ(diagnostics.RequestedAuthority, 33);

	Sensor::setMockValue(SensorType::Tps1, 100);
	EXPECT_TRUE(blended.evaluateAirmass(9000, &diagnostics).Airmass.Valid);
	EXPECT_FLOAT_EQ(diagnostics.RequestedAuthority, 99);
}

TEST_F(BlendedAirmassTest, FlatAuthorityEndpointsStayExactAcrossFractionalInputs) {
	StrictMock<MockVp3d> mapEstimate;
	SpeedDensityAirmass sd(nullptr, mapEstimate);
	AlphaNAirmass alphaN;
	BlendedAirmass blended(sd, alphaN);
	BlendedAirmassDiagnostics diagnostics;

	for (float endpoint : {0, 100}) {
		setTable(config->airmassBlendTable, endpoint);
		// A rounded endpoint must not silently evaluate the unused model.
		// Temperature is shared, so invalidate a branch-specific dependency.
		config->alphaNBaroCompensation = endpoint == 0;
		Sensor::setInvalidMockValue(SensorType::BarometricPressure);
		if (endpoint == 100) {
			config->veLoadBins[2] = config->veLoadBins[1];
		}
		const auto check = [&](float rpm) {
			SCOPED_TRACE(rpm);
			const auto result = blended.evaluateAirmass(rpm, &diagnostics);
			ASSERT_TRUE(result.Airmass.Valid);
			EXPECT_EQ(result.Fault, AirmassInjectionFault::None);
			EXPECT_EQ(diagnostics.RequestedAuthority, endpoint);
			EXPECT_EQ(diagnostics.EffectiveAuthority, endpoint);
			EXPECT_EQ((diagnostics.Flags & BlendedSdEvaluated) != 0, endpoint == 0);
			EXPECT_EQ((diagnostics.Flags & BlendedAlphaNEvaluated) != 0, endpoint == 100);
		};
		for (float tps : {0.0f, 0.01f, 0.33333f, 1.01f, 3.33f, 6.9f, 15.33f, 59.99f, 99.01f, 99.99f, 100.0f}) {
			SCOPED_TRACE(tps);
			Sensor::setMockValue(SensorType::Tps1, tps);
			// The old weighted-sum interpolation gives 100.0000076 and
			// 99.9999924 respectively at these RPMs with TPS zero.
			for (float rpm : {100.125f, 900.8125f, 902.3125f, 9000.125f}) {
				check(rpm);
			}
			for (size_t i = 0; i < AIRMASS_BLEND_RPM_COUNT - 1; i++) {
				const float low = config->airmassBlendRpmBins[i];
				const float high = config->airmassBlendRpmBins[i + 1];
				for (float fraction : {0.00001f, 0.25203125f, 0.25578125f, 0.51f, 0.99999f}) {
					check(low + (high - low) * fraction);
				}
			}
		}
	}
}

TEST_F(BlendedAirmassTest, LocalAuthorityPlateausStayAtExactEndpoints) {
	StrictMock<MockVp3d> mapEstimate;
	SpeedDensityAirmass sd(nullptr, mapEstimate);
	AlphaNAirmass alphaN;
	BlendedAirmass blended(sd, alphaN);
	Sensor::setMockValue(SensorType::Tps1, 99.99f);

	for (float endpoint : {0, 100}) {
		setTable(config->airmassBlendTable, 50);
		for (size_t row : {6, 7}) {
			for (size_t column : {0, 1}) {
				config->airmassBlendTable[row][column] = endpoint;
			}
		}
		for (float rpm : {900.8125f, 902.3125f}) {
			BlendedAirmassDiagnostics diagnostics;
			ASSERT_TRUE(blended.evaluateAirmass(rpm, &diagnostics).Airmass.Valid);
			EXPECT_EQ(diagnostics.RequestedAuthority, endpoint);
			EXPECT_EQ((diagnostics.Flags & BlendedSdEvaluated) != 0, endpoint == 0);
			EXPECT_EQ((diagnostics.Flags & BlendedAlphaNEvaluated) != 0, endpoint == 100);
		}
	}
}

TEST_F(BlendedAirmassTest, FractionalAuthorityNearEndpointsStillEvaluatesBothModels) {
	setTable(config->airmassBlendTable, 0);
	config->airmassBlendTable[7][0] = 100;
	config->airmassBlendTable[7][1] = 100;
	StrictMock<MockVp3d> mapEstimate;
	SpeedDensityAirmass sd(nullptr, mapEstimate);
	AlphaNAirmass alphaN;
	BlendedAirmass blended(sd, alphaN);

	for (float tps : {60.0001f, 99.9999f}) {
		Sensor::setMockValue(SensorType::Tps1, tps);
		BlendedAirmassDiagnostics diagnostics;
		ASSERT_TRUE(blended.evaluateAirmass(900.8125f, &diagnostics).Airmass.Valid);
		EXPECT_GT(diagnostics.RequestedAuthority, 0);
		EXPECT_LT(diagnostics.RequestedAuthority, 100);
		EXPECT_NEAR(diagnostics.RequestedAuthority, (tps - 60) / 40 * 100, 0.00001f);
		EXPECT_TRUE(diagnostics.Flags & BlendedSdEvaluated);
		EXPECT_TRUE(diagnostics.Flags & BlendedAlphaNEvaluated);
	}
}

TEST_F(BlendedAirmassTest, CorrectionsApplyOnceAndLoadsResolveFromFinalComposite) {
	engineConfiguration->useMapEstimateDuringTransient = true;
	engine->module<TpsAccelEnrichment>()->isAboveAccelThreshold = true;
	config->useMapEstimateTable = true;
	Sensor::setMockValue(SensorType::Map, 40);
	Sensor::setMockValue(SensorType::Iat, 27);
	setTable(config->airmassBlendTable, 25);
	configureFlatVeBlend(config->veBlends[0], GPPWM_Tps, GPPWM_Zero, 20);
	configureFlatVeBlend(config->veBlends[1], GPPWM_Iat, GPPWM_FuelLoad, -25);
	engine->engineState.fuelingLoad = 64;

	StrictMock<MockVp3d> mapEstimate;
	EXPECT_CALL(mapEstimate, getValue(2400, 20)).Times(3).WillRepeatedly(Return(75));
	SpeedDensityAirmass sd(nullptr, mapEstimate);
	AlphaNAirmass alphaN;
	BlendedAirmass blended(sd, alphaN);
	const float sdMass = expectedIdealGasMass(4, 4, 60, 75, 300);
	const float alphaNMass = expectedIdealGasMass(4, 4, 40, 101.325f, 300);
	const float expectedMass = (0.75f * sdMass + 0.25f * alphaNMass) * 0.9f;

	engineConfiguration->afrOverrideMode = AFR_None;
	engineConfiguration->ignOverrideMode = AFR_MAP;
	BlendedAirmassDiagnostics diagnostics;
	auto result = blended.evaluateAirmass(2400, &diagnostics);
	ASSERT_TRUE(result.Airmass.Valid);
	EXPECT_NEAR(result.Airmass.Result.CylinderAirmass, expectedMass, EPS4D);
	EXPECT_NEAR(result.NormalizedFilling, 100 * expectedMass / getStandardAirCharge(), EPS4D);
	EXPECT_FLOAT_EQ(diagnostics.Corrections.Blends[0].BlendParameter, 20);
	EXPECT_FLOAT_EQ(diagnostics.Corrections.Blends[0].TableYAxis, 75);
	EXPECT_FLOAT_EQ(diagnostics.Corrections.Blends[1].BlendParameter, 27);
	EXPECT_FLOAT_EQ(diagnostics.Corrections.Blends[1].TableYAxis, 64);
	EXPECT_TRUE(diagnostics.Flags & BlendedEstimateEvaluated);
	EXPECT_TRUE(diagnostics.Flags & BlendedMapEstimateUsed);
	EXPECT_FLOAT_EQ(result.LambdaLoad.Value, 75);
	EXPECT_TRUE(result.LambdaLoad.UsesEstimate);
	EXPECT_FLOAT_EQ(result.IgnitionLoad.Value, 40);
	EXPECT_FALSE(result.IgnitionLoad.UsesEstimate);

	engineConfiguration->afrOverrideMode = AFR_Tps;
	engineConfiguration->ignOverrideMode = AFR_AccPedal;
	result = blended.evaluateAirmass(2400);
	ASSERT_TRUE(result.Airmass.Valid);
	EXPECT_FLOAT_EQ(result.LambdaLoad.Value, 20);
	EXPECT_EQ(result.LambdaLoad.Source, AirmassLoadSource::Tps);
	EXPECT_FLOAT_EQ(result.IgnitionLoad.Value, 30);
	EXPECT_EQ(result.IgnitionLoad.Source, AirmassLoadSource::Pedal);

	engineConfiguration->afrOverrideMode = AFR_CylFilling;
	engineConfiguration->ignOverrideMode = AFR_None;
	result = blended.evaluateAirmass(2400);
	ASSERT_TRUE(result.Airmass.Valid);
	EXPECT_NEAR(result.LambdaLoad.Value, 100 * expectedMass / getStandardAirCharge(), EPS4D);
	EXPECT_EQ(result.LambdaLoad.Source, AirmassLoadSource::CylinderFilling);
	EXPECT_FLOAT_EQ(result.IgnitionLoad.Value, 75);
	EXPECT_EQ(result.IgnitionLoad.Source, AirmassLoadSource::EffectiveMap);
}

TEST_F(BlendedAirmassTest, EndpointsSkipUnusedMapAndBaroDependenciesButShareTemperature) {
	StrictMock<MockVp3d> mapEstimate;
	SpeedDensityAirmass sd(nullptr, mapEstimate);
	AlphaNAirmass alphaN;
	BlendedAirmass blended(sd, alphaN);
	setTable(config->airmassBlendTable, 0);
	config->alphaNBaroCompensation = true;
	Sensor::setInvalidMockValue(SensorType::BarometricPressure);
	config->alphaNTpsBins[2] = config->alphaNTpsBins[1];
	EXPECT_TRUE(blended.evaluateAirmass(2200).Airmass.Valid);
	setTable(config->airmassBlendTable, 100);
	EXPECT_TRUE(blended.evaluateAirmass(2200).Airmass.Valid);
	setLinearCurve(config->alphaNTpsBins, 0, 100, 1);
	EXPECT_TRUE(blended.evaluateAirmass(2200).Airmass.Valid);
	config->alphaNBaroCompensation = false;
	EXPECT_TRUE(blended.evaluateAirmass(2200).Airmass.Valid);
	engine->engineState.sd.tChargeK = NAN;
	for (int authority : {0, 50, 100}) {
		setTable(config->airmassBlendTable, authority);
		EXPECT_TRUE(blended.evaluateAirmass(2200).Airmass.Valid);
	}
	engine->engineState.sd.tChargeK = 300;
	config->veBlends[0].blendParameter = GPPWM_Tps;
	config->veBlends[0].loadBins[2] = config->veBlends[0].loadBins[1];
	EXPECT_EQ(blended.evaluateAirmass(2200).Fault, AirmassInjectionFault::Correction);
}

TEST_F(BlendedAirmassTest, InvalidRawResultUsesHealthyAlternateWithoutMixingNan) {
	setTable(config->airmassBlendTable, 0);
	StrictMock<MockVp3d> invalidVe;
	EXPECT_CALL(invalidVe, getValue(2200, 50)).WillOnce(Return(std::numeric_limits<float>::quiet_NaN()));
	StrictMock<MockVp3d> mapEstimate;
	SpeedDensityAirmass sd(&invalidVe, mapEstimate);
	AlphaNAirmass alphaN;
	BlendedAirmass blended(sd, alphaN);

	BlendedAirmassDiagnostics diagnostics;
	const auto result = blended.evaluateAirmass(2200, &diagnostics);
	EXPECT_TRUE(result.Airmass.Valid);
	EXPECT_TRUE(result.Degraded);
	EXPECT_EQ(result.Fault, AirmassInjectionFault::Result);
	EXPECT_TRUE(diagnostics.Sd.HasValue);
	EXPECT_FALSE(diagnostics.Sd.Valid);
	EXPECT_FLOAT_EQ(diagnostics.RequestedAuthority, 0);
	EXPECT_FLOAT_EQ(diagnostics.EffectiveAuthority, 100);
	EXPECT_TRUE(diagnostics.Flags & BlendedCalculationValid);
	EXPECT_TRUE(diagnostics.Flags & BlendedBranchFallback);
	EXPECT_NEAR(result.Airmass.Result.CylinderAirmass, expectedIdealGasMass(4, 4, 40, 101.325f, 300), EPS4D);
}

TEST_F(BlendedAirmassTest, EstimatedMapPermissionAndMeasuredOverrideRemainDistinct) {
	setTable(config->airmassBlendTable, 100);
	Sensor::setInvalidMockValue(SensorType::Map);
	StrictMock<MockVp3d> mapEstimate;
	EXPECT_CALL(mapEstimate, getValue(2300, 20)).WillOnce(Return(70));
	SpeedDensityAirmass sd(nullptr, mapEstimate);
	AlphaNAirmass alphaN;
	BlendedAirmass blended(sd, alphaN);

	config->useMapEstimateTable = false;
	BlendedAirmassDiagnostics readinessDiagnostics;
	auto missingEstimateReadiness = blended.evaluateAirmass(2300, &readinessDiagnostics);
	EXPECT_TRUE(missingEstimateReadiness.Airmass.Valid);
	EXPECT_TRUE(missingEstimateReadiness.Degraded);
	EXPECT_FLOAT_EQ(missingEstimateReadiness.LambdaLoad.Value, 200);
	EXPECT_FLOAT_EQ(readinessDiagnostics.RequestedAuthority, 100);
	EXPECT_FLOAT_EQ(readinessDiagnostics.EffectiveAuthority, 100);

	config->useMapEstimateTable = true;
	engineConfiguration->afrOverrideMode = AFR_MAP;
	BlendedAirmassDiagnostics loadDiagnostics;
	auto missingMeasuredMap = blended.evaluateAirmass(2300, &loadDiagnostics);
	EXPECT_TRUE(missingMeasuredMap.Airmass.Valid);
	EXPECT_TRUE(missingMeasuredMap.Degraded);
	EXPECT_TRUE(missingMeasuredMap.LambdaLoad.Valid);
	EXPECT_FLOAT_EQ(missingMeasuredMap.LambdaLoad.Value, 200);
	EXPECT_TRUE(missingMeasuredMap.IgnitionLoad.Valid);
	EXPECT_FLOAT_EQ(loadDiagnostics.RequestedAuthority, 100);
	EXPECT_FLOAT_EQ(loadDiagnostics.EffectiveAuthority, 100);
	EXPECT_TRUE(loadDiagnostics.Flags & BlendedCalculationValid);
	EXPECT_TRUE(loadDiagnostics.Flags & BlendedLoadFallback);
}

TEST_F(BlendedAirmassTest, DisabledPermissionSkipsTransientComparisonEvenWhenStoredEnabled) {
	setTable(config->airmassBlendTable, 0);
	engineConfiguration->useMapEstimateDuringTransient = true;
	engine->module<TpsAccelEnrichment>()->isAboveAccelThreshold = true;
	Sensor::setMockValue(SensorType::Map, 40);
	StrictMock<MockVp3d> mapEstimate;
	EXPECT_CALL(mapEstimate, getValue(2300, 20)).WillOnce(Return(30));
	SpeedDensityAirmass sd(nullptr, mapEstimate);
	AlphaNAirmass alphaN;
	BlendedAirmass blended(sd, alphaN);

	BlendedAirmassDiagnostics diagnostics;
	config->useMapEstimateTable = false;
	auto missingReadiness = blended.evaluateAirmass(2300, &diagnostics);
	EXPECT_TRUE(missingReadiness.Airmass.Valid);
	EXPECT_EQ(missingReadiness.Fault, AirmassInjectionFault::None);
	EXPECT_FALSE(diagnostics.Map.HasValue);
	EXPECT_FALSE(diagnostics.Map.UsesEstimate);
	EXPECT_FLOAT_EQ(diagnostics.RequestedAuthority, 0);
	EXPECT_FLOAT_EQ(diagnostics.EffectiveAuthority, 0);

	config->useMapEstimateTable = true;
	auto ready = blended.evaluateAirmass(2300, &diagnostics);
	EXPECT_TRUE(ready.Airmass.Valid);
	EXPECT_FLOAT_EQ(ready.Airmass.Result.EngineLoadPercent, 40);
	EXPECT_TRUE(diagnostics.Flags & BlendedEstimateEvaluated);
	EXPECT_FALSE(diagnostics.Flags & BlendedMapEstimateUsed);
}

TEST_F(BlendedAirmassTest, DryEvaluationDoesNotPublishAndFuelEvaluationIsCoherent) {
	setTable(config->airmassBlendTable, 25);
	StrictMock<MockVp3d> mapEstimate;
	SpeedDensityAirmass sd(nullptr, mapEstimate);
	AlphaNAirmass alphaN;
	BlendedAirmass blended(sd, alphaN);
	const float sdMass = expectedIdealGasMass(4, 4, 60, 50, 300);
	const float alphaNMass = expectedIdealGasMass(4, 4, 40, 101.325f, 300);

	engine->outputChannels.blendedSdMass = 11;
	engine->outputChannels.blendedAlphaNMass = 12;
	engine->outputChannels.blendedRequestedAuthority = 13;
	engine->outputChannels.blendedEffectiveAuthority = 14;
	engine->outputChannels.blendedFlags = 15;
	engine->engineState.currentVe = 16;
	engine->engineState.veTableYAxis = 17;

	BlendedAirmassDiagnostics diagnostics;
	EXPECT_TRUE(blended.evaluateAirmass(2500, &diagnostics).Airmass.Valid);
	EXPECT_NEAR(blended.getAirmass(2500, false).CylinderAirmass, 0.75f * sdMass + 0.25f * alphaNMass, EPS4D);
	EXPECT_FLOAT_EQ(engine->outputChannels.blendedSdMass, 11);
	EXPECT_FLOAT_EQ(engine->outputChannels.blendedAlphaNMass, 12);
	EXPECT_FLOAT_EQ(engine->outputChannels.blendedRequestedAuthority, 13);
	EXPECT_FLOAT_EQ(engine->outputChannels.blendedEffectiveAuthority, 14);
	EXPECT_EQ(engine->outputChannels.blendedFlags, 15);
	EXPECT_FLOAT_EQ(engine->engineState.currentVe, 16);
	EXPECT_FLOAT_EQ(engine->engineState.veTableYAxis, 17);

	const auto live = blended.getAirmassForFuel(2500);
	ASSERT_TRUE(live.Airmass.Valid);
	EXPECT_NEAR(engine->outputChannels.blendedSdMass, sdMass, EPS4D);
	EXPECT_NEAR(engine->outputChannels.blendedAlphaNMass, alphaNMass, EPS4D);
	EXPECT_FLOAT_EQ(engine->outputChannels.blendedRequestedAuthority, 25);
	EXPECT_FLOAT_EQ(engine->outputChannels.blendedEffectiveAuthority, 25);
	EXPECT_FLOAT_EQ(engine->outputChannels.blendedSdLoad, 50);
	EXPECT_FLOAT_EQ(engine->outputChannels.blendedAlphaNLoad, 20);
	EXPECT_FLOAT_EQ(engine->outputChannels.blendedSdVe, 60);
	EXPECT_FLOAT_EQ(engine->outputChannels.blendedAlphaNVe, 40);
	EXPECT_FLOAT_EQ(engine->outputChannels.blendedCorrection, 1);
	EXPECT_TRUE(engine->outputChannels.blendedFlags & BlendedCalculationValid);
	EXPECT_FLOAT_EQ(engine->engineState.currentVe, 0);
	EXPECT_FLOAT_EQ(engine->engineState.veTableYAxis, 0);
	EXPECT_FLOAT_EQ(engine->engineState.idleVeTableYAxis, 0);
}

TEST_F(BlendedAirmassTest, PeriodicFastCallbackPublishesPositiveFuelBeforeGateBecomesReady) {
	Sensor::setMockValue(SensorType::Rpm, 2500);
	EXPECT_FALSE(engine->airmassInjectionState.allowInjection());
	EXPECT_EQ(engine->airmassInjectionState.status(), AirmassInjectionStatus::NotReady);

	engine->periodicFastCallback();

	EXPECT_GT(engine->cylinders[0].getInjectionMass(), 0);
	EXPECT_TRUE(std::isfinite(engine->cylinders[0].getInjectionMass()));
	EXPECT_EQ(engine->airmassInjectionState.status(), AirmassInjectionStatus::Ready);
	EXPECT_TRUE(engine->airmassInjectionState.allowInjection());
}

TEST_F(BlendedAirmassTest, RequiredSensorFaultBlocksSchedulingDespiteAllFuelAdditions) {
	setTable(config->airmassBlendTable, 50);
	engineConfiguration->cranking.baseFuel = 4000;
	engine->engineState.lua.fuelMult = 2;
	engine->engineState.lua.fuelAdd = 3;
	engineConfiguration->tpsAccelLookback = 2;
	setTable(config->tpsTpsAccelTable, 1);
	setLinearCurve(config->tpsTspCorrValuesBins, 0, 7000, 1);
	setArrayValues(config->tpsTspCorrValues, 1);
	initAccelEnrichment();
	engine->module<TpsAccelEnrichment>()->isAboveAccelThreshold = true;
	engine->module<TpsAccelEnrichment>()->tpsFrom = 0;
	engine->module<TpsAccelEnrichment>()->tpsTo = 100;

	Sensor::setMockValue(SensorType::Rpm, 200);
	engine->rpmCalculator.setSpinningUp(getTimeNowNt());
	engine->rpmCalculator.setRpmValue(200);
	ASSERT_TRUE(engine->rpmCalculator.isCranking());
	ASSERT_EQ(engine->airmassInjectionState.status(), AirmassInjectionStatus::Ready);

	Sensor::setInvalidMockValue(SensorType::Tps1);
	Sensor::setInvalidMockValue(SensorType::Map);
	config->useMapEstimateTable = false;
	engine->periodicFastCallback();
	EXPECT_GT(engine->engineState.tpsAccelEnrich, 0);
	EXPECT_EQ(engine->airmassInjectionState.status(), AirmassInjectionStatus::Faulted);
	EXPECT_EQ(engine->airmassInjectionState.fault(), AirmassInjectionFault::Sensor);

	InjectorContext context;
	context.outputsMask = 1;
	const auto now = getTimeNowNt();
	ScheduledAction pulse[] = {
			{now + US2NT(1000), {scheduledStartInjection, context}},
			{now + US2NT(2000), {scheduledEndInjection, context}},
	};
	const auto queuedBefore = engine->scheduler.size();
	EXPECT_FALSE(scheduleFuelCallbacks(pulse, efi::size(pulse)));
	EXPECT_EQ(engine->scheduler.size(), queuedBefore);
	EXPECT_EQ(engine->airmassInjectionState.pendingCallbacks(), 0);
	Sensor::setMockValue(SensorType::Tps1, 20);
	Sensor::setMockValue(SensorType::Map, 80);
	engine->periodicFastCallback();
	EXPECT_TRUE(engine->airmassInjectionState.allowInjection());
	EXPECT_EQ(engine->airmassInjectionState.status(), AirmassInjectionStatus::Ready);
}

TEST_F(BlendedAirmassTest, InvalidStrictFuelConversionBlocksPublicationDespiteAllFuelAdditions) {
	setTable(config->lambdaTable, 0);
	engineConfiguration->cranking.baseFuel = 4000;
	engine->engineState.lua.fuelMult = 2;
	engine->engineState.lua.fuelAdd = 3;
	engineConfiguration->tpsAccelLookback = 2;
	setTable(config->tpsTpsAccelTable, 1);
	setLinearCurve(config->tpsTspCorrValuesBins, 0, 7000, 1);
	setArrayValues(config->tpsTspCorrValues, 1);
	initAccelEnrichment();
	engine->module<TpsAccelEnrichment>()->isAboveAccelThreshold = true;
	engine->module<TpsAccelEnrichment>()->tpsFrom = 0;
	engine->module<TpsAccelEnrichment>()->tpsTo = 100;

	Sensor::setMockValue(SensorType::Rpm, 200);
	engine->rpmCalculator.setSpinningUp(getTimeNowNt());
	engine->rpmCalculator.setRpmValue(200);
	ASSERT_TRUE(engine->rpmCalculator.isCranking());
	// setRpmValue runs the live calculation, so the invalid conversion must
	// already have blocked fuel before another fast callback gets a chance to publish.
	ASSERT_EQ(engine->airmassInjectionState.status(), AirmassInjectionStatus::Faulted);
	ASSERT_EQ(engine->airmassInjectionState.fault(), AirmassInjectionFault::Result);

	engine->periodicFastCallback();

	EXPECT_GT(engine->engineState.tpsAccelEnrich, 0);
	EXPECT_FLOAT_EQ(engine->cylinders[0].getInjectionMass(), 0);
	EXPECT_EQ(engine->airmassInjectionState.status(), AirmassInjectionStatus::Faulted);
	EXPECT_EQ(engine->airmassInjectionState.fault(), AirmassInjectionFault::Result);
	EXPECT_FLOAT_EQ(engine->fuelComputer.getResolvedLambdaLoad(), 0);
	EXPECT_FLOAT_EQ(engine->outputChannels.blendedLambdaLoad, 0);
}

TEST_F(BlendedAirmassTest, NegativeGlobalFuelCorrectionBlocksCompositePublication) {
	engineConfiguration->globalFuelCorrection = -0.5f;
	Sensor::setMockValue(SensorType::Rpm, 2500);

	engine->periodicFastCallback();

	EXPECT_FLOAT_EQ(engine->cylinders[0].getInjectionMass(), 0);
	EXPECT_EQ(engine->airmassInjectionState.status(), AirmassInjectionStatus::Faulted);
	EXPECT_EQ(engine->airmassInjectionState.fault(), AirmassInjectionFault::Result);
	EXPECT_FLOAT_EQ(engine->fuelComputer.getResolvedLambdaLoad(), 0);
	EXPECT_FLOAT_EQ(engine->outputChannels.blendedLambdaLoad, 0);
}

TEST_F(BlendedAirmassTest, LiveStrategyWritesInvalidateOldCalculationAndRecoverAutomatically) {
	Sensor::setMockValue(SensorType::Rpm, 2500);
	engine->periodicFastCallback();
	ASSERT_EQ(engine->airmassInjectionState.status(), AirmassInjectionStatus::Ready);

	auto token = engine->airmassInjectionState.beginCalculation(
			LM_SD_ALPHA_N, 2500, engine->getGlobalConfigurationVersion());
	engine->airmassInjectionState.acceptCalculation();
	ASSERT_TRUE(engine->airmassInjectionState.isCalculationCurrent(token));

	TunerStudio tunerStudio;
	::testing::NiceMock<MockTsChannel> channel;
	constexpr auto modeOffset = offsetof(engine_configuration_s, fuelAlgorithm);
	uint8_t mode = LM_SPEED_DENSITY;
	tunerStudio.handleWriteChunkCommand(&channel, modeOffset, sizeof(mode), &mode);
	mode = LM_SD_ALPHA_N;
	tunerStudio.handleWriteChunkCommand(&channel, modeOffset, sizeof(mode), &mode);

	EXPECT_EQ(engineConfiguration->fuelAlgorithm, LM_SD_ALPHA_N);
	EXPECT_FALSE(engine->airmassInjectionState.isCalculationCurrent(token));
	engine->airmassInjectionState.completeCalculation(token, true);
	EXPECT_EQ(engine->airmassInjectionState.status(), AirmassInjectionStatus::NotReady);
	EXPECT_FALSE(engine->airmassInjectionState.allowInjection());
	engine->periodicFastCallback();
	EXPECT_EQ(engine->airmassInjectionState.status(), AirmassInjectionStatus::Ready);
	EXPECT_TRUE(engine->airmassInjectionState.allowInjection());
}

TEST_F(BlendedAirmassTest, LambdaLoadStagingPreservesValuesAbovePackedAfrRange) {
	setTable(config->airmassBlendTable, 100);
	engineConfiguration->afrOverrideMode = AFR_MAP;
	engineConfiguration->enableStagedInjection = true;
	setTable(config->injectorStagingTable, 0);
	setLinearCurve(config->injectorStagingRpmBins, 0, 5000, 1);
	config->injectorStagingLoadBins[0] = 0;
	config->injectorStagingLoadBins[1] = 600;
	config->injectorStagingLoadBins[2] = 650;
	config->injectorStagingLoadBins[3] = 700;
	config->injectorStagingLoadBins[4] = 800;
	config->injectorStagingLoadBins[5] = 1000;
	setArrayValues(config->injectorStagingTable[3], 60);
	Sensor::setMockValue(SensorType::Map, 700);
	Sensor::setMockValue(SensorType::Rpm, 2500);

	engine->periodicFastCallback();

	EXPECT_FLOAT_EQ(engine->outputChannels.blendedLambdaLoad, 700);
	EXPECT_FLOAT_EQ(engine->fuelComputer.getResolvedLambdaLoad(), 700);
	EXPECT_FLOAT_EQ(engine->fuelComputer.afrTableYAxis, 655.35f);
	EXPECT_FLOAT_EQ(engine->engineState.injectionStage2Fraction, 0.6f);
}

TEST_F(BlendedAirmassTest, SharedTemperatureAndBaroApplyBeforeBlendRegardlessOfStoredHybridOption) {
	StrictMock<MockVp3d> estimate;
	SpeedDensityAirmass sd(nullptr, estimate);
	AlphaNAirmass alphaN;
	BlendedAirmass blended(sd, alphaN);
	Sensor::setMockValue(SensorType::Iat, 10);
	Sensor::setMockValue(SensorType::BarometricPressure, 80);
	config->alphaNBaroCompensation = true;
	config->alphaNBaroReferencePressure = 100;
	configureFlatVeBlend(config->veBlends[0], GPPWM_Tps, GPPWM_Zero, 20);
	for (auto source : {AirmassTemperatureSource::Tcharge, AirmassTemperatureSource::Iat}) {
		config->airmassTemperatureSource = source;
		const float temperature = source == AirmassTemperatureSource::Tcharge ? 300 : 283.15f;
		const float sdMass = expectedIdealGasMass(4, 4, 60, 50, temperature);
		const float alphaMass = expectedIdealGasMass(4, 4, 40, 101.325f, temperature) * 0.8f;
		for (int authority : {0, 25, 100}) {
			setTable(config->airmassBlendTable, authority);
			for (bool hybrid : {false, true}) {
				config->alphaNMultiplyMap = hybrid;
				BlendedAirmassDiagnostics diagnostics;
				const auto evaluation = blended.evaluateAirmass(2200, &diagnostics);
				ASSERT_TRUE(evaluation.Airmass.Valid);
				EXPECT_FLOAT_EQ(diagnostics.TemperatureK, temperature);
				EXPECT_EQ(diagnostics.TemperatureSource, source);
				EXPECT_TRUE(diagnostics.TemperatureValid);
				EXPECT_FALSE(diagnostics.PressureFlags & 8);
				const float weight = authority * 0.01f;
				EXPECT_NEAR(
						evaluation.Airmass.Result.CylinderAirmass,
						((1 - weight) * sdMass + weight * alphaMass) * 1.2f,
						EPS4D);
				EXPECT_FLOAT_EQ(diagnostics.BaroCoefficient, authority == 0 ? 1 : 0.8f);
			}
		}
	}
}

namespace {
struct RevisionIdleController final : public MockIdleController {
	bool isIdlingOrTaper() const override {
		return true;
	}
};
} // namespace

TEST_F(BlendedAirmassTest, IdleOverlayRunsOnlyInsideItsContributingModel) {
	RevisionIdleController idle;
	engine->engineModules.get<IdleController>().set(&idle);
	engineConfiguration->useSeparateVeForIdle = true;
	engineConfiguration->idlePidDeactivationTpsThreshold = 10;
	Sensor::setMockValue(SensorType::DriverThrottleIntent, 0);
	setTable(config->idleVeTable, 20);
	setLinearCurve(config->idleVeLoadBins, 0, 100, 1);
	setLinearCurve(config->idleVeRpmBins, 0, 2500, 1);
	StrictMock<MockVp3d> estimate;
	SpeedDensityAirmass sd(nullptr, estimate);
	AlphaNAirmass alphaN;
	BlendedAirmass blended(sd, alphaN);
	for (auto target : {IdleVeModel::SpeedDensity, IdleVeModel::AlphaN}) {
		config->idleVeModel = target;
		for (auto axis : {IdleVeLoadSource::EffectiveMap, IdleVeLoadSource::Tps}) {
			config->idleVeLoadSource = axis;
			for (int authority : {0, 50, 100}) {
				setTable(config->airmassBlendTable, authority);
				BlendedAirmassDiagnostics diagnostics;
				const auto result = blended.evaluateAirmass(2200, &diagnostics);
				ASSERT_TRUE(result.Airmass.Valid);
				const float sdMass = expectedIdealGasMass(4, 4, target == IdleVeModel::SpeedDensity ? 20 : 60, 50, 300);
				const float alphaMass =
						expectedIdealGasMass(4, 4, target == IdleVeModel::AlphaN ? 20 : 40, 101.325f, 300);
				const float weight = authority * 0.01f;
				EXPECT_NEAR(result.Airmass.Result.CylinderAirmass, (1 - weight) * sdMass + weight * alphaMass, EPS4D);
			}
		}
	}
	// Missing measured MAP used solely by Alpha-N's idle table must be ignored
	// at 0% Alpha-N authority, where SD can use the permitted estimate.
	config->idleVeModel = IdleVeModel::AlphaN;
	config->idleVeLoadSource = IdleVeLoadSource::MeasuredMap;
	config->useMapEstimateTable = true;
	Sensor::setInvalidMockValue(SensorType::Map);
	EXPECT_CALL(estimate, getValue(2200, 20)).Times(2).WillRepeatedly(Return(50));
	setTable(config->airmassBlendTable, 0);
	EXPECT_TRUE(blended.evaluateAirmass(2200).Airmass.Valid);
	setTable(config->airmassBlendTable, 50);
	BlendedAirmassDiagnostics diagnostics;
	const auto recovered = blended.evaluateAirmass(2200, &diagnostics);
	EXPECT_TRUE(recovered.Airmass.Valid);
	EXPECT_TRUE(recovered.Degraded);
	EXPECT_TRUE(diagnostics.Flags & BlendedIdleFallback);
	EXPECT_FLOAT_EQ(diagnostics.AlphaN.IdleWeight, 0);
	EXPECT_FLOAT_EQ(diagnostics.AlphaN.TableValue, 40);
}

TEST_F(BlendedAirmassTest, MapFailurePromotesAlphaNAtEveryAuthorityAndAutomaticallyRecovers) {
	StrictMock<MockVp3d> estimate;
	SpeedDensityAirmass sd(nullptr, estimate);
	AlphaNAirmass alphaN;
	BlendedAirmass blended(sd, alphaN);
	config->useMapEstimateTable = false;
	for (int authority : {0, 25, 50, 75, 100}) {
		setTable(config->airmassBlendTable, authority);
		Sensor::setInvalidMockValue(SensorType::Map);
		BlendedAirmassDiagnostics diagnostics;
		const auto fallback = blended.evaluateAirmass(2200, &diagnostics);
		ASSERT_TRUE(fallback.Airmass.Valid);
		EXPECT_TRUE(fallback.Degraded);
		EXPECT_FLOAT_EQ(diagnostics.RequestedAuthority, authority);
		EXPECT_FLOAT_EQ(diagnostics.EffectiveAuthority, 100);
		EXPECT_NEAR(fallback.Airmass.Result.CylinderAirmass, expectedIdealGasMass(4, 4, 40, 101.325f, 300), EPS4D);
		Sensor::setMockValue(SensorType::Map, 50);
		const auto recovered = blended.evaluateAirmass(2200, &diagnostics);
		EXPECT_TRUE(recovered.Airmass.Valid);
		EXPECT_FALSE(recovered.Degraded);
		EXPECT_FLOAT_EQ(diagnostics.EffectiveAuthority, authority);
	}
}

TEST_F(BlendedAirmassTest, MissingTpsUsesMeasuredMapOrPermittedZeroTpsEstimate) {
	StrictMock<MockVp3d> estimate;
	SpeedDensityAirmass sd(nullptr, estimate);
	AlphaNAirmass alphaN;
	BlendedAirmass blended(sd, alphaN);
	setTable(config->airmassBlendTable, 75);
	Sensor::setInvalidMockValue(SensorType::Tps1);
	BlendedAirmassDiagnostics diagnostics;
	const auto measured = blended.evaluateAirmass(2200, &diagnostics);
	ASSERT_TRUE(measured.Airmass.Valid);
	EXPECT_TRUE(measured.Degraded);
	EXPECT_TRUE(diagnostics.Flags & BlendedAuthorityUnavailable);
	EXPECT_FLOAT_EQ(diagnostics.EffectiveAuthority, 0);
	EXPECT_NEAR(measured.Airmass.Result.CylinderAirmass, expectedIdealGasMass(4, 4, 60, 50, 300), EPS4D);
	Sensor::setInvalidMockValue(SensorType::Map);
	config->useMapEstimateTable = false;
	EXPECT_FALSE(blended.evaluateAirmass(2200).Airmass.Valid);
	config->useMapEstimateTable = true;
	EXPECT_CALL(estimate, getValue(2200, 0)).WillOnce(Return(40));
	const auto estimated = blended.evaluateAirmass(2200, &diagnostics);
	ASSERT_TRUE(estimated.Airmass.Valid);
	EXPECT_TRUE(estimated.Degraded);
	EXPECT_TRUE(diagnostics.Flags & BlendedMapEstimateUsed);
	EXPECT_NEAR(estimated.Airmass.Result.CylinderAirmass, expectedIdealGasMass(4, 4, 60, 40, 300), EPS4D);
	Sensor::setMockValue(SensorType::Map, 50);
	Sensor::setMockValue(SensorType::Tps1, 20);
	const auto recovered = blended.evaluateAirmass(2200, &diagnostics);
	EXPECT_TRUE(recovered.Airmass.Valid);
	EXPECT_FALSE(recovered.Degraded);
	EXPECT_FALSE(diagnostics.Flags & BlendedAuthorityUnavailable);
	EXPECT_FLOAT_EQ(diagnostics.EffectiveAuthority, 75);
}

TEST_F(BlendedAirmassTest, SharedTemperatureBaroAndCorrectionFallbacksRetainUsableMass) {
	StrictMock<MockVp3d> estimate;
	SpeedDensityAirmass sd(nullptr, estimate);
	AlphaNAirmass alphaN;
	BlendedAirmass blended(sd, alphaN);
	setTable(config->airmassBlendTable, 50);
	config->airmassTemperatureSource = AirmassTemperatureSource::Iat;
	config->alphaNBaroCompensation = true;
	config->alphaNBaroReferencePressure = 100;
	Sensor::setInvalidMockValue(SensorType::Iat);
	Sensor::setInvalidMockValue(SensorType::BarometricPressure);
	configureFlatVeBlend(config->veBlends[0], GPPWM_Iat, GPPWM_Zero, 20);
	configureFlatVeBlend(config->veBlends[1], GPPWM_Tps, GPPWM_Zero, 10);
	BlendedAirmassDiagnostics diagnostics;
	const auto fallback = blended.evaluateAirmass(2200, &diagnostics);
	ASSERT_TRUE(fallback.Airmass.Valid);
	EXPECT_TRUE(fallback.Degraded);
	EXPECT_TRUE(diagnostics.Flags & BlendedTemperatureFallback);
	EXPECT_TRUE(diagnostics.Flags & BlendedBaroFallback);
	EXPECT_TRUE(diagnostics.Flags & BlendedCorrectionFallback);
	EXPECT_FLOAT_EQ(diagnostics.TemperatureK, 293.15f);
	EXPECT_FLOAT_EQ(diagnostics.BaroCoefficient, 1);
	EXPECT_FLOAT_EQ(diagnostics.Corrections.Blends[0].Value, 0);
	EXPECT_FLOAT_EQ(diagnostics.Corrections.Blends[1].Value, 10);
	const float sdMass = expectedIdealGasMass(4, 4, 60, 50, 293.15f);
	const float alphaMass = expectedIdealGasMass(4, 4, 40, 101.325f, 293.15f);
	EXPECT_NEAR(fallback.Airmass.Result.CylinderAirmass, (sdMass + alphaMass) * 0.5f * 1.1f, EPS4D);
	Sensor::setMockValue(SensorType::Iat, 20);
	Sensor::setMockValue(SensorType::BarometricPressure, 100);
	EXPECT_FALSE(blended.evaluateAirmass(2200).Degraded);
}
