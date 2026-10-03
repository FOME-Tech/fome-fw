#include "pch.h"

#include "alphan_airmass.h"
#include "blended_airmass.h"
#include "airmass_loads.h"
#include "fuel_math.h"
#include "functional_sensor.h"
#include "init.h"
#include "speed_density_airmass.h"

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

void expectLoad(
		const AirmassLoad& load,
		float value,
		AirmassLoadSource source,
		AirmassLoadUnit unit,
		bool usesEstimate = false) {
	EXPECT_TRUE(load.Valid);
	EXPECT_FLOAT_EQ(load.Value, value);
	EXPECT_EQ(load.Source, source);
	EXPECT_EQ(load.Unit, unit);
	EXPECT_EQ(load.UsesEstimate, usesEstimate);
}

class CountingSensor final : public Sensor {
public:
	CountingSensor(SensorType type, float value)
		: Sensor(type)
		, m_value(value) {}

	SensorResult get() const override {
		ReadCount++;
		return m_value;
	}

	void showInfo(const char*) const override {}

	mutable size_t ReadCount = 0;

private:
	float m_value;
};
} // namespace

TEST(AirmassContext, CapturePreservesMapProvenanceAndResolvesEveryLoadSource) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->displacement = 2.4f;
	setCylinderCount(4);
	engine->engineState.sd.tChargeK = 310;
	engine->engineState.fuelingLoad = 61;
	engine->engineState.ignitionLoad = 62;
	engineConfiguration->afrOverrideMode = AFR_MAP;
	engineConfiguration->ignOverrideMode = AFR_Tps;
	config->useMapEstimateTable = true;
	engineConfiguration->useMapEstimateDuringTransient = true;
	engine->module<TpsAccelEnrichment>()->isAboveAccelThreshold = true;
	Sensor::setMockValue(SensorType::Map, 40);
	Sensor::setMockValue(SensorType::Tps1, 20);
	Sensor::setMockValue(SensorType::AcceleratorPedal, 30);
	Sensor::setMockValue(SensorType::Iat, 35);

	StrictMock<MockVp3d> mapEstimate;
	EXPECT_CALL(mapEstimate, getValue(3000, 20)).Times(2).WillRepeatedly(Return(75));
	SpeedDensityAirmass sd(nullptr, mapEstimate);

	AirmassInputs inputs;
	sd.captureInputs(3000, inputs);
	inputs.Composite = true;

	ASSERT_TRUE(inputs.MeasuredMap.Valid);
	EXPECT_FLOAT_EQ(inputs.MeasuredMap.Value, 40);
	EXPECT_TRUE(inputs.EffectiveMap.HasValue);
	EXPECT_TRUE(inputs.EffectiveMap.Valid);
	EXPECT_TRUE(inputs.EffectiveMap.UsesEstimate);
	EXPECT_FLOAT_EQ(inputs.EffectiveMap.Map, 75);
	EXPECT_FLOAT_EQ(inputs.EffectiveMap.FallbackMap, 75);
	EXPECT_FLOAT_EQ(inputs.PreviousFuelingLoad, 61);
	EXPECT_FLOAT_EQ(inputs.PreviousIgnitionLoad, 62);
	EXPECT_EQ(inputs.LambdaOverride, AFR_MAP);
	EXPECT_EQ(inputs.IgnitionOverride, AFR_Tps);

	// Downstream resolution must use this capture, even after live inputs change.
	Sensor::setMockValue(SensorType::Map, 90);
	Sensor::setMockValue(SensorType::Tps1, 91);
	Sensor::setMockValue(SensorType::AcceleratorPedal, 92);
	engine->engineState.fuelingLoad = 93;
	engine->engineState.ignitionLoad = 94;

	expectLoad(
			resolveAirmassLoad(inputs, 0, AFR_None), 75, AirmassLoadSource::EffectiveMap, AirmassLoadUnit::Kpa, true);
	expectLoad(
			resolveAirmassLoad(inputs, 0, inputs.LambdaOverride),
			40,
			AirmassLoadSource::MeasuredMap,
			AirmassLoadUnit::Kpa);
	expectLoad(
			resolveAirmassLoad(inputs, 0, inputs.IgnitionOverride),
			20,
			AirmassLoadSource::Tps,
			AirmassLoadUnit::Percent);
	expectLoad(resolveAirmassLoad(inputs, 0, AFR_AccPedal), 30, AirmassLoadSource::Pedal, AirmassLoadUnit::Percent);

	const float halfStandardCharge = 0.5f * getStandardAirCharge();
	expectLoad(
			resolveAirmassLoad(inputs, halfStandardCharge, AFR_CylFilling),
			50,
			AirmassLoadSource::CylinderFilling,
			AirmassLoadUnit::Percent);

	// A usable estimate does not make the physically measured MAP source valid.
	inputs.MeasuredMap = unexpected;
	EXPECT_TRUE(resolveAirmassLoad(inputs, 0, AFR_None).Valid);
	auto missingMeasuredMap = resolveAirmassLoad(inputs, 0, AFR_MAP);
	EXPECT_FALSE(missingMeasuredMap.Valid);
	EXPECT_EQ(missingMeasuredMap.Source, AirmassLoadSource::MeasuredMap);

	Sensor::setInvalidMockValue(SensorType::Map);
	Sensor::setMockValue(SensorType::Tps1, 20);
	AirmassInputs estimatedOnly;
	sd.captureInputs(3000, estimatedOnly);
	EXPECT_FALSE(estimatedOnly.MeasuredMap.Valid);
	EXPECT_TRUE(estimatedOnly.EffectiveMap.Valid);
	EXPECT_TRUE(estimatedOnly.EffectiveMap.UsesEstimate);

	Sensor::setInvalidMockValue(SensorType::Tps1);
	EXPECT_CALL(mapEstimate, getValue(3000, 0)).WillOnce(Return(50));
	AirmassInputs invalidEstimate;
	sd.captureInputs(3000, invalidEstimate);
	EXPECT_FALSE(invalidEstimate.Tps.Valid);
	EXPECT_TRUE(invalidEstimate.EffectiveMap.UsesEstimate);
	EXPECT_TRUE(invalidEstimate.EffectiveMap.Valid);
	EXPECT_TRUE(invalidEstimate.EffectiveMap.Fallback);
	EXPECT_TRUE(resolveAirmassLoad(invalidEstimate, 0, AFR_EffectiveMAP).Valid);
	EXPECT_FALSE(resolveAirmassLoad(invalidEstimate, 0, AFR_Tps).Valid);
}

TEST(AirmassContext, ToleratedTpsAndPedalOvertravelUsesClampedCoordinates) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->tpsErrorDetectionTooLow = -10;
	engineConfiguration->tpsErrorDetectionTooHigh = 110;

	AirmassInputs inputs;
	EXPECT_FLOAT_EQ(normalizeAirmassPercent(inputs, expected<float>(-0.1f)).Value, 0);
	EXPECT_FLOAT_EQ(normalizeAirmassPercent(inputs, expected<float>(100.1f)).Value, 100);
	EXPECT_FALSE(normalizeAirmassPercent(inputs, expected<float>(-10.1f)));
	EXPECT_FALSE(normalizeAirmassPercent(inputs, expected<float>(110.1f)));
	EXPECT_FALSE(normalizeAirmassPercent(inputs, expected<float>(NAN)));
	EXPECT_FALSE(normalizeAirmassPercent(inputs, expected<float>(unexpected)));
	EXPECT_FALSE(normalizeAirmassPercent(inputs, expected<float>(999)));
	inputs.TpsToleranceMin = -0.05f;
	inputs.TpsToleranceMax = 105;
	EXPECT_FALSE(normalizeAirmassPercent(inputs, expected<float>(-0.1f)));
	inputs.TpsToleranceMin = -10;
	inputs.TpsToleranceMax = 110;
	inputs.Tps = -0.1f;
	inputs.Pedal = 100.1f;
	expectLoad(resolveAirmassLoad(inputs, 0, AFR_Tps), 0, AirmassLoadSource::Tps, AirmassLoadUnit::Percent);
	expectLoad(resolveAirmassLoad(inputs, 0, AFR_AccPedal), 100, AirmassLoadSource::Pedal, AirmassLoadUnit::Percent);
	inputs.Tps = -10.1f;
	EXPECT_FALSE(resolveAirmassLoad(inputs, 0, AFR_Tps).Valid);
	inputs.Tps = 999;
	EXPECT_FALSE(resolveAirmassLoad(inputs, 0, AFR_Tps).Valid);

	inputs.TpsToleranceMin = -0.05f;
	inputs.TpsToleranceMax = 105;
	inputs.Tps = -0.1f;
	EXPECT_FALSE(resolveAirmassLoad(inputs, 0, AFR_Tps).Valid);
	inputs.Tps = 100.1f;
	expectLoad(resolveAirmassLoad(inputs, 0, AFR_Tps), 100, AirmassLoadSource::Tps, AirmassLoadUnit::Percent);
	inputs.Rpm = 2200;
	inputs.MeasuredMap = 50;
	configureFlatVeBlend(config->veBlends[0], GPPWM_Tps, GPPWM_Map, 20);
	const auto toleratedCorrection = evaluateAirmassCorrections(inputs);
	EXPECT_TRUE(toleratedCorrection.Valid);
	EXPECT_FALSE(toleratedCorrection.Fallback);
	inputs.Tps = 110.1f;
	const auto invalidCorrection = evaluateAirmassCorrections(inputs);
	EXPECT_TRUE(invalidCorrection.Valid);
	EXPECT_TRUE(invalidCorrection.Fallback);

	engineConfiguration->fuelAlgorithm = LM_SPEED_DENSITY;
	engineConfiguration->isInjectionEnabled = true;
	Sensor::setMockValue(SensorType::Map, 50);
	Sensor::setMockValue(SensorType::Tps1, -0.1f);
	Sensor::setMockValue(SensorType::AcceleratorPedal, 100.1f);
	AirmassInputs captured;
	captureAirmassInputs(2200, captured);
	engineConfiguration->tpsErrorDetectionTooLow = 0;
	engineConfiguration->tpsErrorDetectionTooHigh = 100;
	captured.EffectiveMap = {50, 0, false, true, false};
	ASSERT_TRUE(processAirmassConsumerLoads(captured, 10, true));
	EXPECT_FLOAT_EQ(engine->engineState.airmassLoads.Tps, 0);
	EXPECT_FLOAT_EQ(engine->engineState.airmassLoads.Pedal, 100);
}

TEST(AirmassContext, ToleratedTpsCoordinatesDriveEstimateAndBlendedAuthority) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->fuelAlgorithm = LM_SD_ALPHA_N;
	engineConfiguration->useSeparateVeForIdle = false;
	engine->engineState.sd.tChargeK = 300;
	setTable(config->veTable, 60);
	setTable(config->alphaNTable, 40);
	setTable(config->airmassBlendTable, 100);
	config->useMapEstimateTable = true;
	engineConfiguration->useMapEstimateDuringTransient = false;
	Sensor::setInvalidMockValue(SensorType::Map);
	Sensor::setMockValue(SensorType::Iat, 20);

	StrictMock<MockVp3d> estimate;
	AirmassInputs inputs;
	inputs.Rpm = 2200;
	inputs.Tps = -0.1f;
	inputs.MeasuredMap = unexpected;
	EXPECT_CALL(estimate, getValue(2200, 0)).WillOnce(Return(50));
	resolveCapturedMap(inputs, &estimate);
	EXPECT_TRUE(inputs.EffectiveMap.Valid);
	EXPECT_TRUE(inputs.EffectiveMap.UsesEstimate);
	EXPECT_TRUE(inputs.EffectiveMap.Fallback); // MAP is still missing; valid TPS only keeps its estimate coordinate.

	inputs.MeasuredMap = 40;
	inputs.Tps = 100.1f;
	inputs.EffectiveMap = {};
	engineConfiguration->useMapEstimateDuringTransient = true;
	engine->module<TpsAccelEnrichment>()->isAboveAccelThreshold = true;
	EXPECT_CALL(estimate, getValue(2200, 100)).WillOnce(Return(75));
	resolveCapturedMap(inputs, &estimate);
	EXPECT_TRUE(inputs.EffectiveMap.Valid);
	EXPECT_TRUE(inputs.EffectiveMap.UsesEstimate);
	EXPECT_FALSE(inputs.EffectiveMap.Fallback);

	Sensor::setMockValue(SensorType::Map, 50);
	Sensor::setMockValue(SensorType::Tps1, 100.1f);
	engineConfiguration->useMapEstimateDuringTransient = false;
	engine->module<TpsAccelEnrichment>()->isAboveAccelThreshold = false;
	SpeedDensityAirmass sd(nullptr, estimate);
	AlphaNAirmass alphaN;
	BlendedAirmass blended(sd, alphaN);
	BlendedAirmassDiagnostics diagnostics;
	const auto result = blended.evaluateAirmass(2200, &diagnostics);
	ASSERT_TRUE(result.Airmass.Valid);
	EXPECT_FLOAT_EQ(diagnostics.RequestedAuthority, 100);
	EXPECT_FLOAT_EQ(diagnostics.EffectiveAuthority, 100);
	EXPECT_TRUE(diagnostics.Flags & BlendedAlphaNValid);
	EXPECT_FALSE(result.Degraded);
	EXPECT_FALSE(diagnostics.Flags & (BlendedBranchFallback | BlendedAuthorityUnavailable | BlendedLoadFallback));
}

TEST(AirmassContext, RealTpsConverterOvertravelRemainsUsableByAlphaN) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->fuelAlgorithm = LM_ALPHA_N;
	engineConfiguration->useSeparateVeForIdle = false;
	engineConfiguration->tpsMin = 200;
	engineConfiguration->tpsMax = 800;
	engineConfiguration->tps1_1AdcChannel = EFI_ADC_0;
	engineConfiguration->tps1_2AdcChannel = EFI_ADC_NONE;
	engineConfiguration->tpsErrorDetectionTooLow = -10;
	engineConfiguration->tpsErrorDetectionTooHigh = 110;
	engineConfiguration->isInjectionEnabled = true;
	setTable(config->alphaNTable, 40);
	Sensor::setMockValue(SensorType::Iat, 20);
	Sensor::setMockValue(SensorType::Map, 50);
	initTps();
	auto* sensor =
			static_cast<FunctionalSensor*>(const_cast<Sensor*>(Sensor::getSensorOfType(SensorType::Tps1Primary)));
	ASSERT_NE(sensor, nullptr);
	AlphaNAirmass alphaN;
	AirmassInputs normalizationLimits;
	engine->rpmCalculator.setRpmValue(2000);
	sensor->postRawValue(1.0f, getTimeNowNt());
	engine->engineState.periodicFastCallback();
	ASSERT_TRUE(engine->airmassInjectionState.allowInjection());
	for (const auto [voltage, coordinate] : {std::pair{0.997f, 0.0f}, std::pair{4.003f, 100.0f}}) {
		SCOPED_TRACE(voltage);
		sensor->postRawValue(voltage, getTimeNowNt());
		const auto rawTps = Sensor::get(SensorType::Tps1);
		ASSERT_TRUE(rawTps.Valid);
		EXPECT_TRUE(rawTps.Value < 0 || rawTps.Value > 100);
		EXPECT_FLOAT_EQ(normalizeAirmassPercent(normalizationLimits, expected<float>(rawTps.Value)).Value, coordinate);
		EXPECT_TRUE(alphaN.evaluateAirmass(2000).Valid);
		engine->engineState.periodicFastCallback();
		EXPECT_TRUE(engine->airmassInjectionState.allowInjection());
		EXPECT_GT(engine->cylinders[0].getInjectionMass(), 0);
	}
	deinitTps();
}

TEST(AirmassContext, StrictCaptureValidatesAnEstimateBeforeInterpolation) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	config->useMapEstimateTable = true;
	Sensor::setMockValue(SensorType::Map, 55);
	Sensor::setMockValue(SensorType::Tps1, 20);

	StrictMock<MockVp3d> mapEstimate;
	SpeedDensityAirmass sd(nullptr, mapEstimate);
	const float originalTpsBin = config->mapEstimateTpsBins[2];
	const float originalCell = config->mapEstimateTable[0][0];
	config->mapEstimateTpsBins[2] = config->mapEstimateTpsBins[1];
	config->mapEstimateTable[0][0] = 650;

	// A healthy measured MAP does not depend on an unused estimate calibration.
	AirmassInputs measuredOnly;
	sd.captureInputs(2600, measuredOnly);
	EXPECT_TRUE(measuredOnly.EffectiveMap.Valid);
	EXPECT_FALSE(measuredOnly.EffectiveMap.HasValue);
	EXPECT_FALSE(measuredOnly.EffectiveMap.UsesEstimate);
	EXPECT_FLOAT_EQ(measuredOnly.EffectiveMap.Map, 55);

	Sensor::setInvalidMockValue(SensorType::Map);
	AirmassInputs invalidAxes;
	sd.captureInputs(2600, invalidAxes);
	EXPECT_FALSE(invalidAxes.EffectiveMap.Valid);

	config->mapEstimateTpsBins[2] = originalTpsBin;
	AirmassInputs invalidCell;
	sd.captureInputs(2600, invalidCell);
	EXPECT_FALSE(invalidCell.EffectiveMap.Valid);

	config->mapEstimateTable[0][0] = originalCell;
	Sensor::setInvalidMockValue(SensorType::Tps1);
	EXPECT_CALL(mapEstimate, getValue(2600, 0)).WillOnce(Return(50));
	AirmassInputs invalidTps;
	sd.captureInputs(2600, invalidTps);
	EXPECT_TRUE(invalidTps.EffectiveMap.Valid);
	EXPECT_TRUE(invalidTps.EffectiveMap.Fallback);
	EXPECT_FALSE(invalidTps.Tps.Valid);

	Sensor::setMockValue(SensorType::Tps1, 20);
	EXPECT_CALL(mapEstimate, getValue(2600, 20)).WillOnce(Return(70));
	AirmassInputs validEstimate;
	sd.captureInputs(2600, validEstimate);
	EXPECT_TRUE(validEstimate.EffectiveMap.Valid);
	EXPECT_TRUE(validEstimate.EffectiveMap.HasValue);
	EXPECT_TRUE(validEstimate.EffectiveMap.UsesEstimate);
	EXPECT_FLOAT_EQ(validEstimate.EffectiveMap.Map, 70);
}

TEST(AirmassContext, RawModelsAndCorrectionsUseOneSnapshotAndMatchStandaloneResults) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->displacement = 3.2f;
	config->airmassTemperatureSource = AirmassTemperatureSource::Tcharge;
	setCylinderCount(4);
	engine->engineState.sd.tChargeK = 310;
	setTable(config->veTable, 60);
	setTable(config->alphaNTable, 50);
	configureFlatVeBlend(config->veBlends[0], GPPWM_Tps, GPPWM_Map, 20);
	configureFlatVeBlend(config->veBlends[1], GPPWM_Iat, GPPWM_FuelLoad, -25);
	engine->engineState.fuelingLoad = 64;
	Sensor::setMockValue(SensorType::Map, 45);
	Sensor::setMockValue(SensorType::Tps1, 22);
	Sensor::setMockValue(SensorType::AcceleratorPedal, 31);
	Sensor::setMockValue(SensorType::Iat, 27);

	StrictMock<MockVp3d> mapEstimate;
	SpeedDensityAirmass sd(nullptr, mapEstimate);
	AlphaNAirmass alphaN;

	AirmassInputs inputs;
	sd.captureInputs(2400, inputs);
	const auto standaloneSd = sd.evaluateAirmass(2400);
	const auto standaloneAlphaN = alphaN.evaluateAirmass(2400);

	Sensor::setMockValue(SensorType::Map, 80);
	Sensor::setMockValue(SensorType::Tps1, 81);
	Sensor::setMockValue(SensorType::Iat, 82);
	engine->engineState.fuelingLoad = 83;

	RawAirmassDiagnostics sdDiagnostics;
	RawAirmassDiagnostics alphaNDiagnostics;
	const auto rawSd = sd.evaluateRawAirmass(inputs, &sdDiagnostics);
	const auto rawAlphaN = alphaN.evaluateRawAirmass(inputs, &alphaNDiagnostics);
	VeCorrectionDiagnostics correctionDiagnostics;
	const auto corrections = evaluateAirmassCorrections(inputs, &correctionDiagnostics);

	ASSERT_TRUE(rawSd.Valid);
	ASSERT_TRUE(rawAlphaN.Valid);
	ASSERT_TRUE(corrections.Valid);
	EXPECT_TRUE(sdDiagnostics.HasValue);
	EXPECT_TRUE(sdDiagnostics.Valid);
	EXPECT_FLOAT_EQ(sdDiagnostics.TableValue, 60);
	EXPECT_TRUE(alphaNDiagnostics.HasValue);
	EXPECT_TRUE(alphaNDiagnostics.Valid);
	EXPECT_FLOAT_EQ(alphaNDiagnostics.TableValue, 50);
	EXPECT_NEAR(rawSd.Result.CylinderAirmass, expectedIdealGasMass(3.2f, 4, 60, 45, 310), EPS4D);
	EXPECT_FLOAT_EQ(rawSd.Result.EngineLoadPercent, 45);
	EXPECT_NEAR(rawAlphaN.Result.CylinderAirmass, expectedIdealGasMass(3.2f, 4, 50, 101.325f, 310), EPS4D);
	EXPECT_FLOAT_EQ(rawAlphaN.Result.EngineLoadPercent, 22);

	// Raw model values exclude common corrections. +20% and -25% compound to 0.9.
	EXPECT_FLOAT_EQ(corrections.Multiplier, 0.9f);
	EXPECT_TRUE(correctionDiagnostics.HasValue);
	EXPECT_TRUE(correctionDiagnostics.Valid);
	EXPECT_FLOAT_EQ(correctionDiagnostics.Blends[0].BlendParameter, 22);
	EXPECT_FLOAT_EQ(correctionDiagnostics.Blends[0].TableYAxis, 45);
	EXPECT_FLOAT_EQ(correctionDiagnostics.Blends[1].BlendParameter, 27);
	EXPECT_FLOAT_EQ(correctionDiagnostics.Blends[1].TableYAxis, 64);
	EXPECT_NEAR(standaloneSd.Result.CylinderAirmass, rawSd.Result.CylinderAirmass * corrections.Multiplier, EPS4D);
	EXPECT_NEAR(
			standaloneAlphaN.Result.CylinderAirmass, rawAlphaN.Result.CylinderAirmass * corrections.Multiplier, EPS4D);
	EXPECT_FLOAT_EQ(standaloneSd.Result.EngineLoadPercent, rawSd.Result.EngineLoadPercent);
	EXPECT_FLOAT_EQ(standaloneAlphaN.Result.EngineLoadPercent, rawAlphaN.Result.EngineLoadPercent);

	const float originalBin = config->veBlends[0].loadBins[2];
	config->veBlends[0].loadBins[2] = config->veBlends[0].loadBins[1];
	const auto invalidAxis = evaluateAirmassCorrections(inputs);
	EXPECT_TRUE(invalidAxis.Valid);
	EXPECT_TRUE(invalidAxis.Fallback);
	EXPECT_FLOAT_EQ(invalidAxis.Multiplier, 0.75f);
	config->veBlends[0].loadBins[2] = originalBin;
}

TEST(AirmassContext, SelectedTemperatureValidityIsSharedByBothModels) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engine->engineState.sd.tChargeK = 310;
	Sensor::setMockValue(SensorType::Map, 50);
	Sensor::setMockValue(SensorType::Tps1, 18);
	Sensor::setInvalidMockValue(SensorType::Iat);
	StrictMock<MockVp3d> mapEstimate;
	SpeedDensityAirmass sd(nullptr, mapEstimate);
	AlphaNAirmass alphaN;
	AirmassInputs inputs;
	config->airmassTemperatureSource = AirmassTemperatureSource::Tcharge;
	sd.captureInputs(2000, inputs);
	EXPECT_TRUE(inputs.TemperatureValid);
	EXPECT_TRUE(sd.evaluateRawAirmass(inputs).Valid);
	EXPECT_TRUE(alphaN.evaluateRawAirmass(inputs).Valid);
	config->airmassTemperatureSource = AirmassTemperatureSource::Iat;
	sd.captureInputs(2000, inputs);
	EXPECT_TRUE(inputs.TemperatureValid);
	EXPECT_TRUE(inputs.TemperatureFallback);
	EXPECT_FALSE(inputs.Iat.Valid);
	EXPECT_FLOAT_EQ(inputs.TemperatureK, 293.15f);
	EXPECT_TRUE(sd.evaluateRawAirmass(inputs).Valid);
	EXPECT_TRUE(alphaN.evaluateRawAirmass(inputs).Valid);
	EXPECT_TRUE(alphaN.evaluateAirmass(2000).Valid);
	Sensor::setMockValue(SensorType::Iat, 27);
	sd.captureInputs(2000, inputs);
	EXPECT_FLOAT_EQ(inputs.TemperatureK, 300.15f);
	Sensor::setInvalidMockValue(SensorType::Iat);
	// Both raw branches use the same retained capture after the sensor changes.
	EXPECT_TRUE(sd.evaluateRawAirmass(inputs).Valid);
	EXPECT_TRUE(alphaN.evaluateRawAirmass(inputs).Valid);
}

TEST(AirmassContext, RepeatedNonCoreCorrectionChannelIsSampledOnce) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	CountingSensor auxTemperature(SensorType::AuxTemp1, 33);
	ASSERT_TRUE(auxTemperature.Register());

	configureFlatVeBlend(config->veBlends[0], GPPWM_AuxTemp1, GPPWM_AuxTemp1, 10);
	configureFlatVeBlend(config->veBlends[1], GPPWM_AuxTemp1, GPPWM_AuxTemp1, 20);
	AirmassInputs inputs;
	inputs.Rpm = 2500;
	inputs.EffectiveMap = {60, 60, true, true, false};

	const auto correction = evaluateAirmassCorrections(inputs);
	EXPECT_TRUE(correction.Valid);
	EXPECT_FLOAT_EQ(correction.Multiplier, 1.32f);
	EXPECT_EQ(auxTemperature.ReadCount, 1u);

	auxTemperature.unregister();
}

TEST(AirmassRevision, MapEstimatePermissionControlsFallbackAndTransientComparison) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	StrictMock<MockVp3d> estimate;
	SpeedDensityAirmass sd(nullptr, estimate);
	Sensor::setMockValue(SensorType::Tps1, 20);
	Sensor::setInvalidMockValue(SensorType::Map);
	engineConfiguration->useMapEstimateDuringTransient = true;
	engine->module<TpsAccelEnrichment>()->isAboveAccelThreshold = true;
	config->useMapEstimateTable = false;
	auto disabled = sd.evaluateMap(2200);
	EXPECT_FALSE(disabled.Valid);
	EXPECT_FALSE(disabled.HasValue);
	Sensor::setMockValue(SensorType::Map, 40);
	EXPECT_FLOAT_EQ(sd.evaluateMap(2200).Map, 40);
	config->useMapEstimateTable = true;
	EXPECT_CALL(estimate, getValue(2200, 20)).Times(2).WillRepeatedly(Return(75));
	EXPECT_FLOAT_EQ(sd.evaluateMap(2200).Map, 75);
	engineConfiguration->useMapEstimateDuringTransient = false;
	EXPECT_FLOAT_EQ(sd.evaluateMap(2200).Map, 40);
	Sensor::setInvalidMockValue(SensorType::Map);
	EXPECT_FLOAT_EQ(sd.evaluateMap(2200).Map, 75);
}

TEST(AirmassRevision, HybridPressurePolicyIsExplicitAndSuppressesAutomaticBaro) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->fuelAlgorithm = LM_ALPHA_N;
	engine->engineState.sd.tChargeK = 320;
	setTable(config->alphaNTable, 65);
	Sensor::setMockValue(SensorType::Tps1, 20);
	Sensor::setMockValue(SensorType::Map, 55);
	Sensor::setMockValue(SensorType::Iat, 10);
	Sensor::setMockValue(SensorType::BarometricPressure, 80);
	config->alphaNBaroReferencePressure = 100;
	AlphaNAirmass alphaN;
	for (auto source : {AirmassTemperatureSource::Tcharge, AirmassTemperatureSource::Iat}) {
		config->airmassTemperatureSource = source;
		config->alphaNBaroCompensation = false;
		config->alphaNMultiplyMap = false;
		const auto pure = alphaN.evaluateAirmass(2200);
		ASSERT_TRUE(pure.Valid);
		config->alphaNMultiplyMap = true;
		config->alphaNBaroCompensation = true;
		AirmassDiagnostics diagnostics;
		const auto hybrid = alphaN.evaluateAirmass(2200, &diagnostics);
		ASSERT_TRUE(hybrid.Valid);
		EXPECT_NEAR(hybrid.Result.CylinderAirmass, pure.Result.CylinderAirmass * 55 / 101.325f, EPS4D);
		EXPECT_FLOAT_EQ(diagnostics.BaroCoefficient, 1);
		EXPECT_TRUE(diagnostics.PressureFlags & 8);
		AirmassInputs inputs;
		captureAirmassInputs(2200, inputs);
		RawAirmassDiagnostics raw;
		const auto explicitPure = alphaN.evaluateRawAirmass(inputs, &raw, AlphaNPressurePolicy::PureReference);
		EXPECT_NEAR(explicitPure.Result.CylinderAirmass, pure.Result.CylinderAirmass * 0.8f, EPS4D);
		EXPECT_FLOAT_EQ(raw.BaroCoefficient, 0.8f);
	}
	Sensor::setInvalidMockValue(SensorType::Map);
	EXPECT_FALSE(alphaN.evaluateAirmass(2200).Valid);
	config->alphaNMultiplyMap = false;
	EXPECT_TRUE(alphaN.evaluateAirmass(2200).Valid);
	Sensor::setInvalidMockValue(SensorType::BarometricPressure);
	const auto missingBaro = alphaN.evaluateAirmass(2200);
	EXPECT_TRUE(missingBaro.Valid);
	EXPECT_TRUE(missingBaro.Degraded);
	config->alphaNBaroCompensation = false;
	EXPECT_TRUE(alphaN.evaluateAirmass(2200).Valid);
}

TEST(AirmassRevision, BaroReferenceAndCapturedPressureAreValidated) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engine->engineState.sd.tChargeK = 300;
	Sensor::setMockValue(SensorType::Tps1, 20);
	Sensor::setMockValue(SensorType::BarometricPressure, 90);
	config->alphaNBaroCompensation = true;
	config->alphaNBaroReferencePressure = 90;
	AlphaNAirmass alphaN;
	AirmassInputs inputs;
	captureAirmassInputs(2000, inputs);
	RawAirmassDiagnostics diagnostics;
	auto reference = alphaN.evaluateRawAirmass(inputs, &diagnostics);
	ASSERT_TRUE(reference.Valid);
	EXPECT_FLOAT_EQ(diagnostics.BaroCoefficient, 1);
	Sensor::setMockValue(SensorType::BarometricPressure, 75);
	EXPECT_FLOAT_EQ(alphaN.evaluateRawAirmass(inputs).Result.CylinderAirmass, reference.Result.CylinderAirmass);
	captureAirmassInputs(2000, inputs);
	EXPECT_NEAR(
			alphaN.evaluateRawAirmass(inputs).Result.CylinderAirmass,
			reference.Result.CylinderAirmass * 75 / 90,
			EPS4D);
	for (float invalid : {0.0f, -1.0f, NAN, INFINITY, std::numeric_limits<float>::min()}) {
		config->alphaNBaroReferencePressure = invalid;
		const auto fallback = alphaN.evaluateRawAirmass(inputs, &diagnostics);
		EXPECT_TRUE(fallback.Valid);
		EXPECT_TRUE(fallback.Degraded);
		EXPECT_TRUE(diagnostics.BaroFallback);
		EXPECT_FLOAT_EQ(diagnostics.BaroCoefficient, 1);
		EXPECT_FLOAT_EQ(fallback.Result.CylinderAirmass, reference.Result.CylinderAirmass);
	}
	config->alphaNBaroReferencePressure = 90;
	inputs.BarometricPressure = unexpected;
	EXPECT_TRUE(alphaN.evaluateRawAirmass(inputs).Valid);
	EXPECT_TRUE(alphaN.evaluateRawAirmass(inputs).Degraded);
	config->alphaNBaroCompensation = false;
	EXPECT_TRUE(alphaN.evaluateRawAirmass(inputs).Valid);
}

TEST(AirmassRevision, IdleVeBelongsToOneBranchAndUsesItsOwnAxis) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->useSeparateVeForIdle = true;
	engineConfiguration->idlePidDeactivationTpsThreshold = 10;
	engine->engineState.sd.tChargeK = 300;
	setTable(config->veTable, 80);
	setTable(config->alphaNTable, 80);
	setTable(config->idleVeTable, 40);
	setLinearCurve(config->idleVeLoadBins, 0, 100, 1);
	setLinearCurve(config->idleVeRpmBins, 0, 2500, 1);
	Sensor::setMockValue(SensorType::Map, 55);
	Sensor::setMockValue(SensorType::Tps1, 20);
	StrictMock<MockVp3d> estimate;
	SpeedDensityAirmass sd(nullptr, estimate);
	AlphaNAirmass alphaN;
	AirmassInputs inputs;
	captureAirmassInputs(2000, inputs);
	inputs.Composite = true;
	inputs.IdleActive = true;
	for (auto target : {IdleVeModel::SpeedDensity, IdleVeModel::AlphaN}) {
		config->idleVeModel = target;
		for (auto axis : {IdleVeLoadSource::EffectiveMap, IdleVeLoadSource::Tps, IdleVeLoadSource::MeasuredMap}) {
			config->idleVeLoadSource = axis;
			for (float intent : {0.0f, 5.0f, 7.5f, 10.0f}) {
				inputs.DriverThrottleIntent = intent;
				RawAirmassDiagnostics sdDiagnostics, anDiagnostics;
				ASSERT_TRUE(sd.evaluateRawAirmass(inputs, &sdDiagnostics).Valid);
				ASSERT_TRUE(alphaN.evaluateRawAirmass(inputs, &anDiagnostics).Valid);
				const auto& owned = target == IdleVeModel::SpeedDensity ? sdDiagnostics : anDiagnostics;
				const auto& other = target == IdleVeModel::SpeedDensity ? anDiagnostics : sdDiagnostics;
				EXPECT_FLOAT_EQ(other.TableValue, 80);
				EXPECT_FLOAT_EQ(other.IdleWeight, 0);
				EXPECT_FLOAT_EQ(owned.TableValue, intent <= 5 ? 40 : intent == 7.5f ? 60 : 80);
				if (intent < 10) {
					EXPECT_FLOAT_EQ(owned.IdleLoad, axis == IdleVeLoadSource::Tps ? 20 : 55);
				}
			}
		}
	}
	config->idleVeModel = IdleVeModel::AlphaN;
	config->idleVeLoadSource = IdleVeLoadSource::MeasuredMap;
	inputs.DriverThrottleIntent = 0;
	inputs.MeasuredMap = unexpected;
	EXPECT_TRUE(sd.evaluateRawAirmass(inputs).Valid);
	RawAirmassDiagnostics missingIdleMap;
	EXPECT_TRUE(alphaN.evaluateRawAirmass(inputs, &missingIdleMap).Valid);
	EXPECT_TRUE(missingIdleMap.IdleFallback);
	EXPECT_FLOAT_EQ(missingIdleMap.TableValue, 80);
	EXPECT_FLOAT_EQ(missingIdleMap.IdleWeight, 0);
	inputs.DriverThrottleIntent = unexpected;
	EXPECT_TRUE(alphaN.evaluateRawAirmass(inputs, &missingIdleMap).Valid);
	EXPECT_TRUE(missingIdleMap.IdleFallback);
	EXPECT_FLOAT_EQ(missingIdleMap.TableValue, 80);
	inputs.DriverThrottleIntent = 10;
	EXPECT_TRUE(alphaN.evaluateRawAirmass(inputs).Valid);
	inputs.DriverThrottleIntent = 0;
	inputs.IdleActive = false;
	EXPECT_TRUE(alphaN.evaluateRawAirmass(inputs).Valid);
}

namespace {
void configureMapIndependentAirmass(engine_load_mode_e mode) {
	engineConfiguration->fuelAlgorithm = mode;
	engineConfiguration->isInjectionEnabled = true;
	engineConfiguration->isIgnitionEnabled = true;
	engineConfiguration->useSeparateVeForIdle = false;
	engineConfiguration->useSeparateAdvanceForIdle = false;
	engineConfiguration->enableTrailingSparks = false;
	engineConfiguration->fuelClosedLoopCorrectionEnabled = false;
	engineConfiguration->lambdaProtectionEnable = false;
	engineConfiguration->afrOverrideMode = AFR_Tps;
	engineConfiguration->ignOverrideMode = AFR_Tps;
	config->airmassTemperatureSource = AirmassTemperatureSource::Iat;
	config->alphaNMultiplyMap = false;
	config->alphaNBaroCompensation = false;
	config->useMapEstimateTable = false;
	config->injectionPhaseLoadSource = AFR_Tps;
	config->ignitionIatLoadSource = AFR_Tps;
	for (auto& source : config->fuelTrimLoadSource) {
		source = AFR_Tps;
	}
	for (auto& source : config->ignitionTrimLoadSource) {
		source = AFR_Tps;
	}
	setTable(config->alphaNTable, 60);
	setTable(config->airmassBlendTable, 100);
	setTable(config->lambdaTable, 1);
	setTable(config->ignitionTable, 45);
	Sensor::setMockValue(SensorType::Tps1, 20);
	Sensor::setMockValue(SensorType::Iat, 20);
	Sensor::setMockValue(SensorType::Clt, 80);
	Sensor::setInvalidMockValue(SensorType::Map);
	Sensor::setMockValue(SensorType::Rpm, 2000);
}
} // namespace

TEST(AirmassRevision, DisabledIgnitionDoesNotRequireItsStoredMapOrRunTimingTables) {
	for (auto mode : {LM_ALPHA_N, LM_SD_ALPHA_N}) {
		EngineTestHelper eth(engine_type_e::TEST_ENGINE);
		configureMapIndependentAirmass(mode);
		engineConfiguration->isIgnitionEnabled = false;
		engineConfiguration->ignOverrideMode = AFR_MAP;
		engineConfiguration->enableTrailingSparks = true;
		config->trailingSparkLoadSource = AFR_MAP;
		engine->ignitionState.luaTimingMult = NAN;
		engine->ignitionState.timingIatCorrection = 10;
		engine->ignitionState.sparkDwell = 3;
		engine->torqueReductionController.setReductionRequest(0.5f);
		engine->engineState.periodicFastCallback();
		ASSERT_TRUE(engine->engineState.airmassCalculationValid);
		if (mode == LM_SD_ALPHA_N) {
			EXPECT_EQ(engine->airmassInjectionState.status(), AirmassInjectionStatus::Ready);
			EXPECT_FALSE(engine->outputChannels.blendedFlags & (BlendedLoadFallback | BlendedMapFallback));
			StrictMock<MockVp3d> estimate;
			SpeedDensityAirmass sd(nullptr, estimate);
			AlphaNAirmass alphaN;
			BlendedAirmass blended(sd, alphaN);
			EXPECT_FALSE(blended.evaluateAirmass(2000).Degraded);
		}
		EXPECT_GT(engine->cylinders[0].getInjectionMass(), 0);
		EXPECT_FLOAT_EQ(engine->engineState.ignitionLoad, 0);
		EXPECT_FLOAT_EQ(engine->outputChannels.ignitionAdvance, 0);
		EXPECT_FLOAT_EQ(engine->engineState.trailingSparkAngle, 0);
		EXPECT_FLOAT_EQ(engine->ignitionState.timingIatCorrection, 0);
		EXPECT_FLOAT_EQ(engine->ignitionState.sparkDwell, 0);
		EXPECT_FLOAT_EQ(engine->torqueReductionController.reductionRequest, 50);
		// Re-enabling ignition uses the upstream load substitute for missing MAP.
		engineConfiguration->isIgnitionEnabled = true;
		engine->ignitionState.luaTimingMult = 1;
		engine->engineState.periodicFastCallback();
		EXPECT_TRUE(engine->engineState.airmassCalculationValid);
		EXPECT_GT(engine->cylinders[0].getInjectionMass(), 0);
		EXPECT_FLOAT_EQ(engine->engineState.ignitionLoad, 200);
	}
}

TEST(AirmassRevision, UnusedLambdaTargetDoesNotBlockAirCalculationOrPublishFuel) {
	for (auto mode : {LM_ALPHA_N, LM_SD_ALPHA_N}) {
		EngineTestHelper eth(engine_type_e::TEST_ENGINE);
		configureMapIndependentAirmass(mode);
		engineConfiguration->isInjectionEnabled = false;
		engineConfiguration->afrOverrideMode = AFR_MAP;
		engineConfiguration->enableStagedInjection = true;
		config->stagingLoadSource = AFR_MAP;
		config->injectionPhaseLoadSource = AFR_MAP;
		setTable(config->lambdaTable, 0);
		engine->fuelComputer.afrTableYAxis = 99;
		engine->fuelComputer.targetLambda = 1.2f;
		engine->fuelComputer.targetAFR = 17;
		engine->fuelComputer.stoichiometricRatio = 14.7f;
		engineConfiguration->cranking.baseFuel = 4000;
		engine->engineState.lua.fuelAdd = 3;
		engine->engineState.periodicFastCallback();
		ASSERT_TRUE(engine->engineState.airmassCalculationValid);
		ASSERT_FALSE(isAirmassLambdaTargetRequired());
		if (mode == LM_SD_ALPHA_N) {
			EXPECT_EQ(engine->airmassInjectionState.status(), AirmassInjectionStatus::Ready);
			EXPECT_FALSE(engine->outputChannels.blendedFlags & (BlendedLoadFallback | BlendedMapFallback));
		}
		EXPECT_GT(engine->fuelComputer.sdAirMassInOneCylinder, 0);
		EXPECT_FLOAT_EQ(engine->fuelComputer.afrTableYAxis, 0);
		EXPECT_FLOAT_EQ(engine->fuelComputer.targetLambda, 0);
		EXPECT_FLOAT_EQ(engine->fuelComputer.targetAFR, 0);
		EXPECT_FLOAT_EQ(engine->fuelComputer.stoichiometricRatio, 0);
		EXPECT_FLOAT_EQ(engine->engineState.injectionDuration, 0);
		EXPECT_FLOAT_EQ(engine->engineState.injectionDurationStage2, 0);
		EXPECT_FLOAT_EQ(engine->engineState.baseFuel, 0);
		EXPECT_FLOAT_EQ(engine->cylinders[0].getInjectionMass(), 0);
		engineConfiguration->isInjectionEnabled = true;
		engine->engineState.periodicFastCallback();
		EXPECT_FALSE(engine->engineState.airmassCalculationValid);
	}
}

TEST(AirmassRevision, StandaloneFuelConversionRejectsInvalidTargetBeforePackedPublication) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureMapIndependentAirmass(LM_ALPHA_N);
	engine->engineState.periodicFastCallback();
	ASSERT_TRUE(engine->airmassInjectionState.allowInjection());
	const auto token =
			engine->airmassInjectionState.beginCalculation(LM_ALPHA_N, 2000, engine->getGlobalConfigurationVersion());
	// The packed calibration can represent zero lambda, which is an invalid
	// fuel conversion target. NaN cannot be stored in its integer encoding.
	setTable(config->lambdaTable, 0);
	EXPECT_FLOAT_EQ(getCycleInjectionMass(2000, false), 0);
	// Known invalid target closes admission before the fast callback finishes.
	EXPECT_FALSE(engine->airmassInjectionState.allowInjection());
	engine->airmassInjectionState.completeCalculation(token, true);
	EXPECT_FALSE(engine->airmassInjectionState.allowInjection());
	EXPECT_FALSE(engine->engineState.airmassCalculationValid);
	EXPECT_FLOAT_EQ(engine->fuelComputer.afrTableYAxis, 0);
	EXPECT_FLOAT_EQ(engine->fuelComputer.targetLambda, 0);
	EXPECT_FLOAT_EQ(engine->fuelComputer.targetAFR, 0);
	EXPECT_FLOAT_EQ(engine->fuelComputer.stoichiometricRatio, 0);
	EXPECT_FLOAT_EQ(engine->cylinders[0].getInjectionMass(), 0);
}

TEST(AirmassRevision, StandaloneInputFailureClosesAdmissionBeforeFinalPublication) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureMapIndependentAirmass(LM_ALPHA_N);
	engine->engineState.periodicFastCallback();
	ASSERT_TRUE(engine->airmassInjectionState.allowInjection());
	const auto token =
			engine->airmassInjectionState.beginCalculation(LM_ALPHA_N, 2000, engine->getGlobalConfigurationVersion());
	Sensor::setInvalidMockValue(SensorType::Tps1);
	EXPECT_FLOAT_EQ(getCycleInjectionMass(2000, false), 0);
	EXPECT_FALSE(engine->airmassInjectionState.allowInjection());
	EXPECT_FLOAT_EQ(engine->cylinders[0].getInjectionMass(), 0);
	EXPECT_FLOAT_EQ(engine->engineState.injectionDuration, 0);
	engine->airmassInjectionState.completeCalculation(token, true);
	EXPECT_FALSE(engine->airmassInjectionState.allowInjection());

	Sensor::setMockValue(SensorType::Tps1, 20);
	engine->engineState.periodicFastCallback();
	EXPECT_TRUE(engine->airmassInjectionState.allowInjection());
}

TEST(AirmassRevision, InvalidAuthorityCalibrationUsesHealthyBranchWithoutInterpolation) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->fuelAlgorithm = LM_SD_ALPHA_N;
	engineConfiguration->useSeparateVeForIdle = false;
	engine->engineState.sd.tChargeK = 300;
	Sensor::setMockValue(SensorType::Map, 50);
	Sensor::setMockValue(SensorType::Tps1, 20);
	setTable(config->veTable, 60);
	setTable(config->alphaNTable, 40);
	StrictMock<MockVp3d> estimate;
	SpeedDensityAirmass sd(nullptr, estimate);
	AlphaNAirmass alphaN;
	BlendedAirmass blended(sd, alphaN);
	config->airmassBlendTpsBins[2] = config->airmassBlendTpsBins[1];
	BlendedAirmassDiagnostics diagnostics;
	const auto fallback = blended.evaluateAirmass(2200, &diagnostics);
	ASSERT_TRUE(fallback.Airmass.Valid);
	EXPECT_TRUE(fallback.Degraded);
	EXPECT_EQ(fallback.Fault, AirmassInjectionFault::Configuration);
	EXPECT_TRUE(diagnostics.Flags & BlendedAuthorityUnavailable);
	EXPECT_FLOAT_EQ(diagnostics.EffectiveAuthority, 0);
	EXPECT_FLOAT_EQ(fallback.Airmass.Result.CylinderAirmass, sd.evaluateAirmass(2200).Result.CylinderAirmass);
	Sensor::setInvalidMockValue(SensorType::Map);
	config->useMapEstimateTable = false;
	const auto alternate = blended.evaluateAirmass(2200, &diagnostics);
	EXPECT_TRUE(alternate.Airmass.Valid);
	EXPECT_FLOAT_EQ(diagnostics.EffectiveAuthority, 100);
}

TEST(AirmassRevision, InvalidIdleOwnerRetainsBothMainModelCalibrations) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->useSeparateVeForIdle = true;
	engine->engineState.sd.tChargeK = 300;
	Sensor::setMockValue(SensorType::Map, 50);
	Sensor::setMockValue(SensorType::Tps1, 20);
	setTable(config->veTable, 60);
	setTable(config->alphaNTable, 40);
	setTable(config->idleVeTable, 10);
	config->idleVeModel = static_cast<IdleVeModel>(255);
	StrictMock<MockVp3d> estimate;
	SpeedDensityAirmass sd(nullptr, estimate);
	AlphaNAirmass alphaN;
	AirmassInputs inputs;
	captureAirmassInputs(2200, inputs);
	inputs.Composite = true;
	inputs.IdleActive = true;
	inputs.DriverThrottleIntent = 0;
	RawAirmassDiagnostics diagnostics;
	EXPECT_TRUE(sd.evaluateRawAirmass(inputs, &diagnostics).Valid);
	EXPECT_TRUE(diagnostics.IdleFallback);
	EXPECT_FLOAT_EQ(diagnostics.TableValue, 60);
	EXPECT_TRUE(alphaN.evaluateRawAirmass(inputs, &diagnostics).Valid);
	EXPECT_TRUE(diagnostics.IdleFallback);
	EXPECT_FLOAT_EQ(diagnostics.TableValue, 40);
}
