#include "pch.h"

#include "airmass_loads.h"
#include "airmass.h"
#include "gppwm_channel.h"
#include "vvt.h"

namespace {
AirmassInputs consumerInputs() {
	AirmassInputs inputs;
	captureAirmassInputs(2000, inputs, nullptr, false);
	inputs.Rpm = 2000;
	inputs.MeasuredMap = 80;
	inputs.EffectiveMap.Map = 70;
	inputs.EffectiveMap.Valid = true;
	inputs.EffectiveMap.UsesEstimate = true;
	inputs.Tps = 20;
	inputs.Pedal = 40;
	inputs.Iat = 20;
	inputs.DriverThrottleIntent = 20;
	inputs.Displacement = 2;
	inputs.CylinderCount = 4;
	inputs.Composite = true;
	inputs.Model = LM_SD_ALPHA_N;
	inputs.NativeLoad = 70;
	inputs.LambdaOverride = AFR_Tps;
	inputs.IgnitionOverride = AFR_Tps;
	return inputs;
}

class AirmassConsumers : public ::testing::Test {
protected:
	AirmassConsumers()
		: eth(engine_type_e::TEST_ENGINE) {
		engineConfiguration->fuelAlgorithm = LM_SD_ALPHA_N;
		engineConfiguration->useSeparateVeForIdle = false;
		engineConfiguration->alphaNUseIat = true;
		engineConfiguration->isInjectionEnabled = true;
		engineConfiguration->isIgnitionEnabled = true;
		engineConfiguration->timingMode = TM_DYNAMIC;
		engineConfiguration->useSeparateAdvanceForIdle = false;
		engineConfiguration->enableTrailingSparks = true;
		setCylinderCount(4);
		Sensor::setMockValue(SensorType::Map, 80);
		Sensor::setMockValue(SensorType::Tps1, 20);
		Sensor::setMockValue(SensorType::Iat, 20);
		Sensor::setMockValue(SensorType::Clt, 80);
		setTable(config->veTable, 60);
		setTable(config->alphaNTable, 40);
		setTable(config->airmassBlendTable, 0);
		setTable(config->lambdaTable, 1);
		setTable(config->ignitionTable, 10);
		setTable(config->ignitionIatCorrTable, 0);
		setArrayValues(config->cltTimingExtra, 0);
		setLinearCurve(config->fuelTrimLoadBins, 0, 100, 1);
		setLinearCurve(config->ignTrimLoadBins, 0, 100, 1);
		for (size_t row = 0; row < efi::size(config->fuelTrims[0].table); row++) {
			for (size_t cylinder = 0; cylinder < 2; cylinder++) {
				setArrayValues(config->fuelTrims[cylinder].table[row], config->fuelTrimLoadBins[row] / 10.0f);
				setArrayValues(config->ignTrims[cylinder].table[row], config->ignTrimLoadBins[row] / 10.0f);
			}
		}
		config->fuelTrimLoadSource[0] = AFR_Tps;
		config->fuelTrimLoadSource[1] = AFR_MAP;
		config->ignitionTrimLoadSource[0] = AFR_Tps;
		config->ignitionTrimLoadSource[1] = AFR_MAP;
		engine->rpmCalculator.setRpmValue(2000);
	}
	void calculate() {
		engine->engineState.periodicFastCallback();
	}
	EngineTestHelper eth;
};
} // namespace

TEST_F(AirmassConsumers, CylinderTrimsResolveEachSelectedCoordinate) {
	calculate();
	ASSERT_TRUE(engine->engineState.airmassCalculationValid);
	const float firstMass = engine->cylinders[0].getInjectionMass();
	const float secondMass = engine->cylinders[1].getInjectionMass();
	ASSERT_GT(firstMass, 0);
	EXPECT_GT(secondMass, firstMass * 1.04f);
	const float firstTiming = engine->cylinders[0].getIgnitionTimingBtdc();
	const float secondTiming = engine->cylinders[1].getIgnitionTimingBtdc();
	EXPECT_GT(secondTiming - firstTiming, 5);

	config->fuelTrimLoadSource[0] = AFR_MAP;
	config->ignitionTrimLoadSource[0] = AFR_MAP;
	calculate();
	EXPECT_FLOAT_EQ(engine->cylinders[0].getInjectionMass(), engine->cylinders[1].getInjectionMass());
	EXPECT_FLOAT_EQ(engine->cylinders[0].getIgnitionTimingBtdc(), engine->cylinders[1].getIgnitionTimingBtdc());
}

TEST_F(AirmassConsumers, PhaseStagingIatAndTrailingUseTheirOwnSources) {
	engineConfiguration->enableStagedInjection = true;
	engineConfiguration->afrOverrideMode = AFR_MAP;
	engineConfiguration->ignOverrideMode = AFR_MAP;
	config->injectionPhaseLoadSource = AFR_Tps;
	config->stagingLoadSource = AFR_Tps;
	config->ignitionIatLoadSource = AFR_Tps;
	config->trailingSparkLoadSource = AFR_Tps;
	setLinearCurve(config->injPhaseLoadBins, 0, 100, 1);
	setLinearCurve(config->injectorStagingLoadBins, 0, 100, 1);
	setLinearCurve(config->ignitionIatCorrLoadBins, 0, 100, 1);
	setLinearCurve(config->trailingIgnitionLoadBins, 0, 100, 1);
	for (size_t row = 0; row < efi::size(config->injectionPhase); row++) {
		setArrayValues(config->injectionPhase[row], config->injPhaseLoadBins[row]);
	}
	for (size_t row = 0; row < efi::size(config->injectorStagingTable); row++) {
		setArrayValues(config->injectorStagingTable[row], config->injectorStagingLoadBins[row]);
	}
	for (size_t row = 0; row < efi::size(config->ignitionIatCorrTable); row++) {
		setArrayValues(config->ignitionIatCorrTable[row], config->ignitionIatCorrLoadBins[row] / 10.0f);
	}
	for (size_t row = 0; row < efi::size(config->trailingIgnitionTable); row++) {
		setArrayValues(config->trailingIgnitionTable[row], config->trailingIgnitionLoadBins[row] / 10.0f);
	}
	calculate();
	ASSERT_TRUE(engine->engineState.airmassCalculationValid);
	EXPECT_NEAR(engine->engineState.injectionOffset, 20, 0.1f);
	EXPECT_NEAR(engine->engineState.injectionStage2Fraction, 0.20f, 0.01f);
	EXPECT_NEAR(engine->ignitionState.timingIatCorrection, 2, 0.2f);
	EXPECT_NEAR(engine->engineState.trailingSparkAngle, 2, 0.2f);
	EXPECT_FLOAT_EQ(engine->engineState.ignitionLoad, 80);
	EXPECT_FLOAT_EQ(engine->fuelComputer.getResolvedLambdaLoad(), 80);

	config->injectionPhaseLoadSource = AFR_MAP;
	calculate();
	EXPECT_NEAR(engine->engineState.injectionOffset, 80, 0.1f);
	EXPECT_NEAR(engine->engineState.injectionStage2Fraction, 0.20f, 0.01f);
	EXPECT_NEAR(engine->ignitionState.timingIatCorrection, 2, 0.2f);
	EXPECT_NEAR(engine->engineState.trailingSparkAngle, 2, 0.2f);
}

TEST_F(AirmassConsumers, InvalidAirmassCannotBeRevivedByLuaFuelOrInjectorLag) {
	calculate();
	ASSERT_TRUE(engine->engineState.airmassCalculationValid);
	engine->engineState.lua.fuelAdd = 1;
	Sensor::setInvalidMockValue(SensorType::Map);
	Sensor::setInvalidMockValue(SensorType::Tps1);
	config->useMapEstimateTable = false;
	config->mapEstimateRpmBins[1] = config->mapEstimateRpmBins[0];
	calculate();
	EXPECT_FALSE(engine->engineState.airmassCalculationValid);
	EXPECT_FLOAT_EQ(engine->engineState.injectionDuration, 0);
	EXPECT_FLOAT_EQ(engine->engineState.injectionDurationStage2, 0);
	for (size_t cylinder = 0; cylinder < 4; cylinder++) {
		EXPECT_FLOAT_EQ(engine->cylinders[cylinder].getInjectionMass(), 0);
	}
}

TEST_F(AirmassConsumers, DisabledIgnitionTrimDoesNotRejectFuelPublication) {
	engineConfiguration->isIgnitionEnabled = false;
	config->ignitionTrimLoadSource[0] = AFR_AccPedal;
	Sensor::setInvalidMockValue(SensorType::AcceleratorPedal);
	calculate();
	EXPECT_TRUE(engine->engineState.airmassCalculationValid);
	EXPECT_TRUE(engine->airmassInjectionState.allowInjection());
	EXPECT_GT(engine->cylinders[0].getInjectionMass(), 0);
}

TEST_F(AirmassConsumers, LambdaAndIgnitionOverridesUseUpstreamSubstitutesAndRecover) {
	engineConfiguration->afrOverrideMode = AFR_MAP;
	engineConfiguration->ignOverrideMode = AFR_AccPedal;
	config->useMapEstimateTable = true;
	setTable(config->mapEstimateTable, 70);
	Sensor::setInvalidMockValue(SensorType::Map);
	Sensor::setInvalidMockValue(SensorType::AcceleratorPedal);
	calculate();
	ASSERT_TRUE(engine->engineState.airmassCalculationValid);
	EXPECT_TRUE(engine->airmassInjectionState.allowInjection());
	EXPECT_GT(engine->cylinders[0].getInjectionMass(), 0);
	EXPECT_FLOAT_EQ(engine->fuelComputer.getResolvedLambdaLoad(), 200);
	EXPECT_FLOAT_EQ(engine->engineState.ignitionLoad, 100);
	EXPECT_FLOAT_EQ(getEffectiveAirmassMap().value_or(0), 70);
	EXPECT_FALSE(engine->engineState.airmassLoads.ValidSources & (1u << AFR_MAP));
	EXPECT_FALSE(engine->engineState.airmassLoads.ValidSources & (1u << AFR_AccPedal));
	EXPECT_TRUE(hasAirmassLoadFallback());

	Sensor::setMockValue(SensorType::Map, 80);
	Sensor::setMockValue(SensorType::AcceleratorPedal, 40);
	calculate();
	EXPECT_TRUE(engine->airmassInjectionState.allowInjection());
	EXPECT_GT(engine->cylinders[0].getInjectionMass(), 0);
	EXPECT_FLOAT_EQ(engine->fuelComputer.getResolvedLambdaLoad(), 80);
	EXPECT_FLOAT_EQ(engine->engineState.ignitionLoad, 40);
	EXPECT_FALSE(hasAirmassLoadFallback());
}

TEST_F(AirmassConsumers, OptionalActuatorFailureStaysLocalAndRecoversWithoutFuelCut) {
	auto& pwmConfig = engineConfiguration->gppwm[0];
	pwmConfig.pin = Gpio::A0;
	pwmConfig.rpmAxis = GPPWM_Zero;
	pwmConfig.loadAxis = GPPWM_AccelPedal;
	pwmConfig.dutyIfError = 21;
	GppwmChannel pwm;
	::testing::StrictMock<MockVp3d> pwmTable;
	EXPECT_CALL(pwmTable, getValue(0, 40)).WillOnce(::testing::Return(35));
	pwm.init(false, nullptr, nullptr, &pwmTable, &pwmConfig);

	engineConfiguration->fanPin = Gpio::A1;
	engineConfiguration->fan1UsePwmMode = true;
	engineConfiguration->fan1PwmXAxis = GPPWM_AccelPedal;
	engineConfiguration->fanPwmSafetyDuty = 90;
	setTable(config->fan1DutyAcOff, 25);
	auto& fan = engine->module<FanControl1>();
	fan->m_state = true;

	engineConfiguration->vvtPins[0] = Gpio::A2;
	engineConfiguration->vvtIntakeYAxisOverride = GPPWM_AccelPedal;
	::testing::StrictMock<MockVp3d> vvtTable;
	EXPECT_CALL(vvtTable, getValue(2000, 40)).WillOnce(::testing::Return(10));
	VvtController vvt(0, 0, 0);
	vvt.init(&vvtTable, nullptr);
	Sensor::setMockValue(SensorType::Rpm, 2000);
	Sensor::setInvalidMockValue(SensorType::AcceleratorPedal);

	calculate();
	EXPECT_TRUE(engine->engineState.airmassCalculationValid);
	EXPECT_TRUE(engine->airmassInjectionState.allowInjection());
	EXPECT_GT(engine->cylinders[0].getInjectionMass(), 0);
	EXPECT_FALSE(hasAirmassLoadFallback());
	EXPECT_FLOAT_EQ(pwm.getOutput().Result, 21);
	EXPECT_FLOAT_EQ(fan->getAcOffDuty(80), 90);
	EXPECT_FALSE(vvt.getSetpoint().Valid);

	Sensor::setMockValue(SensorType::AcceleratorPedal, 40);
	calculate();
	EXPECT_TRUE(engine->airmassInjectionState.allowInjection());
	EXPECT_GT(engine->cylinders[0].getInjectionMass(), 0);
	EXPECT_FLOAT_EQ(pwm.getOutput().Result, 35);
	EXPECT_FLOAT_EQ(fan->getAcOffDuty(80), 25);
	EXPECT_FLOAT_EQ(vvt.getSetpoint().value_or(0), 10);
}

TEST(AirmassConsumerSnapshot, MissingPressureUsesCoordinateFallbackWithoutFakingEffectiveMap) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->fuelAlgorithm = LM_SD_ALPHA_N;
	auto inputs = consumerInputs();
	inputs.MeasuredMap = unexpected;
	inputs.EffectiveMap.Valid = false;
	inputs.EffectiveMap.Map = NAN;
	inputs.LambdaOverride = AFR_None;
	inputs.IgnitionOverride = AFR_None;
	ASSERT_TRUE(processAirmassConsumerLoads(inputs, 0.3f, true));
	EXPECT_FLOAT_EQ(getAirmassSelectedLoad(AFR_None, 0), 200);
	EXPECT_FLOAT_EQ(getAirmassSelectedLoad(AFR_EffectiveMAP, 0), 200);
	EXPECT_FALSE(getEffectiveAirmassMap().Valid);
	EXPECT_FALSE(readGppwmChannel(GPPWM_EffectiveMap).Valid);
	EXPECT_FALSE(engine->engineState.airmassLoads.ValidSources & (1u << AFR_EffectiveMAP));
	EXPECT_TRUE(hasAirmassLoadFallback());
}

TEST(AirmassConsumerSnapshot, SelectedSourcesAreIndependentAndUseCapturedFullPrecision) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->fuelAlgorithm = LM_SD_ALPHA_N;
	auto inputs = consumerInputs();
	inputs.EffectiveMap.Map = 700.125f;
	inputs.NativeLoad = inputs.EffectiveMap.Map;
	config->injectionPhaseLoadSource = AFR_EffectiveMAP;
	config->fuelTrimLoadSource[0] = AFR_Tps;
	config->fuelTrimLoadSource[1] = AFR_MAP;
	ASSERT_TRUE(processAirmassConsumerLoads(inputs, 0.3f, true));
	EXPECT_FLOAT_EQ(getAirmassConsumerLoad(AirmassConsumer::InjectionPhase), 700.125f);
	EXPECT_FLOAT_EQ(getAirmassConsumerLoad(AirmassConsumer::FuelTrim, 0), 20);
	EXPECT_FLOAT_EQ(getAirmassConsumerLoad(AirmassConsumer::FuelTrim, 1), 80);
	Sensor::setMockValue(SensorType::Map, 99);
	Sensor::setMockValue(SensorType::Tps1, 45);
	engineConfiguration->ignOverrideMode = AFR_AccPedal;
	engineConfiguration->afrOverrideMode = AFR_CylFilling;
	EXPECT_FLOAT_EQ(getAirmassConsumerLoad(AirmassConsumer::FuelTrim, 0), 20);
	EXPECT_FLOAT_EQ(getAirmassConsumerLoad(AirmassConsumer::FuelTrim, 1), 80);
	EXPECT_FLOAT_EQ(getEffectiveAirmassMap().value_or(0), 700.125f);
	engine->engineState.fuelingLoad = 1;
	engine->engineState.ignitionLoad = 2;
	EXPECT_FLOAT_EQ(readGppwmChannel(GPPWM_FuelLoad).value_or(0), 700.125f);
	EXPECT_FLOAT_EQ(readGppwmChannel(GPPWM_IgnLoad).value_or(0), 40);
}

TEST(AirmassConsumerSnapshot, DryEvaluationPreservesPublicationAndMissingMeasuredMapStaysInvalid) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->fuelAlgorithm = LM_SD_ALPHA_N;
	auto inputs = consumerInputs();
	config->injectionPhaseLoadSource = AFR_MAP;
	ASSERT_TRUE(processAirmassConsumerLoads(inputs, 0.3f, true));
	const auto cursor = engine->outputChannels.injectionPhaseLoad;
	inputs.MeasuredMap = unexpected;
	inputs.Tps = 90;
	EXPECT_TRUE(processAirmassConsumerLoads(inputs, 0.4f, false));
	EXPECT_TRUE(engine->engineState.airmassLoads.Valid);
	EXPECT_FLOAT_EQ(engine->engineState.airmassLoads.Tps, 20);
	EXPECT_FLOAT_EQ(engine->outputChannels.injectionPhaseLoad, cursor);
	EXPECT_FLOAT_EQ(getAirmassConsumerLoad(AirmassConsumer::InjectionPhase), 80);

	EXPECT_TRUE(processAirmassConsumerLoads(inputs, 0.4f, true));
	EXPECT_TRUE(engine->engineState.airmassLoads.Valid);
	EXPECT_FALSE(engine->engineState.airmassLoads.ValidSources & (1u << AFR_MAP));
	EXPECT_FLOAT_EQ(getAirmassConsumerLoad(AirmassConsumer::InjectionPhase), 200);
	EXPECT_TRUE(hasAirmassLoadFallback());
	EXPECT_FLOAT_EQ(getEffectiveAirmassMap().value_or(0), 70);
	EXPECT_TRUE(std::isnan(engine->engineState.airmassLoads.Map));
	inputs.MeasuredMap = 85.125f;
	ASSERT_TRUE(processAirmassConsumerLoads(inputs, 0.4f, true));
	EXPECT_FLOAT_EQ(getAirmassConsumerLoad(AirmassConsumer::InjectionPhase), 85.125f);
	EXPECT_FALSE(hasAirmassLoadFallback());
}

TEST(AirmassConsumerSnapshot, InactiveSourcesDoNotBecomeDependenciesAndTuneWriteInvalidates) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->fuelAlgorithm = LM_SD_ALPHA_N;
	auto inputs = consumerInputs();
	inputs.Pedal = unexpected;
	config->stagingLoadSource = AFR_AccPedal;
	engineConfiguration->enableStagedInjection = false;
	ASSERT_TRUE(processAirmassConsumerLoads(inputs, 0.3f, true));
	EXPECT_FLOAT_EQ(getAirmassConsumerLoad(AirmassConsumer::Staging), 100);
	engineConfiguration->enableStagedInjection = true;
	EXPECT_TRUE(processAirmassConsumerLoads(inputs, 0.3f, false));
	engineConfiguration->enableStagedInjection = false;
	ASSERT_TRUE(processAirmassConsumerLoads(inputs, 0.3f, true));
	engine->airmassInjectionState.onConfigurationWrite(LM_SD_ALPHA_N, false);
	EXPECT_TRUE(std::isnan(getAirmassConsumerLoad(AirmassConsumer::InjectionPhase)));
	EXPECT_FALSE(getEffectiveAirmassMap().Valid);
}

TEST(AirmassConsumerSnapshot, StaleCaptureCannotRepublishAfterStopOrLiveWrite) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->fuelAlgorithm = LM_SD_ALPHA_N;
	config->injectionPhaseLoadSource = AFR_Tps;
	for (const bool stop : {false, true}) {
		SCOPED_TRACE(stop);
		auto inputs = consumerInputs();
		ASSERT_TRUE(processAirmassConsumerLoads(inputs, 0.3f, true));
		inputs.Tps = 90;
		if (stop) {
			engine->airmassInjectionState.onEngineStop();
		} else {
			engine->airmassInjectionState.onConfigurationWrite(LM_SD_ALPHA_N, false);
		}
		const auto epoch = engine->airmassInjectionState.publicationEpoch();
		engine->outputChannels.injectionPhaseLoad = 35;
		EXPECT_TRUE(processAirmassConsumerLoads(inputs, 0.3f, false));
		EXPECT_EQ(engine->airmassInjectionState.publicationEpoch(), epoch);
		EXPECT_FALSE(processAirmassConsumerLoads(inputs, 0.3f, true));
		EXPECT_FALSE(engine->engineState.airmassLoads.Valid);
		EXPECT_FLOAT_EQ(engine->outputChannels.injectionPhaseLoad, 35);
		EXPECT_FALSE(getEffectiveAirmassMap().Valid);

		const auto fresh = consumerInputs();
		ASSERT_TRUE(processAirmassConsumerLoads(fresh, 0.3f, true));
		EXPECT_FALSE(processAirmassConsumerLoads(inputs, 0.3f, true));
		EXPECT_TRUE(engine->engineState.airmassLoads.Valid);
		EXPECT_FLOAT_EQ(getAirmassConsumerLoad(AirmassConsumer::InjectionPhase), 20);
	}
}

TEST(AirmassConsumerSnapshot, DisabledInjectionStillRequiresTargetWhenStftUsesIt) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->fuelAlgorithm = LM_SD_ALPHA_N;
	engineConfiguration->isInjectionEnabled = false;
	engineConfiguration->fuelClosedLoopCorrectionEnabled = false;
	engineConfiguration->lambdaProtectionEnable = false;
	auto inputs = consumerInputs();
	inputs.LambdaOverride = AFR_AccPedal;
	inputs.Pedal = unexpected;
	ASSERT_TRUE(processAirmassConsumerLoads(inputs, 0.3f, true));
	EXPECT_FALSE(isAirmassLambdaTargetRequired());

	engineConfiguration->fuelClosedLoopCorrectionEnabled = true;
	engineConfiguration->stft.startupDelay = 5;
	engineConfiguration->stft.minClt = 60;
	engineConfiguration->stft.minLambda = 0.7f;
	engineConfiguration->stft.maxLambda = 1.2f;
	engineConfiguration->noFuelTrimAfterDfcoTime = 0;
	engine->rpmCalculator.setRpmValue(2000);
	// The first positive RPM invokes a fast callback and advances the publication epoch.
	inputs = consumerInputs();
	inputs.LambdaOverride = AFR_AccPedal;
	inputs.Pedal = unexpected;
	Sensor::setMockValue(SensorType::Clt, 80);
	Sensor::setMockValue(SensorType::Lambda1, 1);
	for (const auto sensor : {SensorType::Lambda2, SensorType::Lambda3, SensorType::Lambda4}) {
		Sensor::setInvalidMockValue(sensor);
	}
	setTimeNowUs(10e6);
	engine->fuelComputer.running.timeSinceCrankingInSecs = 4;
	ASSERT_TRUE(processAirmassConsumerLoads(inputs, 0.3f, true));
	EXPECT_FALSE(isAirmassLambdaTargetRequired());
	engine->fuelComputer.running.timeSinceCrankingInSecs = 10;
	EXPECT_TRUE(processAirmassConsumerLoads(inputs, 0.3f, true));
	EXPECT_TRUE(isAirmassLambdaTargetRequired());
	EXPECT_FLOAT_EQ(getAirmassSelectedLoad(AFR_AccPedal, 0), 100);
	inputs.Pedal = 40;
	ASSERT_TRUE(processAirmassConsumerLoads(inputs, 0.3f, true));
	EXPECT_TRUE(isAirmassLambdaTargetRequired());

	inputs.Pedal = unexpected;
	Sensor::setMockValue(SensorType::Lambda1, 1.5f);
	ASSERT_TRUE(processAirmassConsumerLoads(inputs, 0.3f, true));
	EXPECT_FALSE(isAirmassLambdaTargetRequired());
	config->stftLoadSource = AFR_AccPedal;
	EXPECT_TRUE(processAirmassConsumerLoads(inputs, 0.3f, false));
	engineConfiguration->fuelClosedLoopCorrectionEnabled = false;
	ASSERT_TRUE(processAirmassConsumerLoads(inputs, 0.3f, true));
	EXPECT_FALSE(isAirmassLambdaTargetRequired());
}

TEST(AirmassConsumerSnapshot, DisabledInjectionStillRequiresTargetForActiveLambdaProtection) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->fuelAlgorithm = LM_SD_ALPHA_N;
	engineConfiguration->isInjectionEnabled = false;
	engineConfiguration->fuelClosedLoopCorrectionEnabled = false;
	engineConfiguration->lambdaProtectionEnable = true;
	engineConfiguration->lambdaProtectionMinRpm = 1000;
	engineConfiguration->lambdaProtectionMinLoad = 50;
	engineConfiguration->lambdaProtectionMinTps = 0;
	engineConfiguration->noFuelTrimAfterDfcoTime = 0;
	config->lambdaMonitorLoadSource = AFR_MAP;
	config->lambdaDeviationLoadSource = AFR_Tps;
	Sensor::setMockValue(SensorType::Lambda1, 1);
	setTimeNowUs(10e6);
	auto inputs = consumerInputs();
	inputs.LambdaOverride = AFR_AccPedal;
	inputs.Pedal = unexpected;
	EXPECT_TRUE(processAirmassConsumerLoads(inputs, 0.3f, true));
	EXPECT_TRUE(isAirmassLambdaTargetRequired());
	EXPECT_FLOAT_EQ(getAirmassSelectedLoad(AFR_AccPedal, 0), 100);
	inputs.Pedal = 40;
	ASSERT_TRUE(processAirmassConsumerLoads(inputs, 0.3f, true));
	EXPECT_TRUE(isAirmassLambdaTargetRequired());

	inputs.Pedal = unexpected;
	inputs.MeasuredMap = 40;
	ASSERT_TRUE(processAirmassConsumerLoads(inputs, 0.3f, true));
	EXPECT_FALSE(isAirmassLambdaTargetRequired());
	inputs.MeasuredMap = 80;
	Sensor::setInvalidMockValue(SensorType::Lambda1);
	ASSERT_TRUE(processAirmassConsumerLoads(inputs, 0.3f, true));
	EXPECT_FALSE(isAirmassLambdaTargetRequired());
}

TEST(AirmassConsumerSnapshot, DisabledIgnitionGppwmAliasUsesUpstreamOverrideFallback) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->fuelAlgorithm = LM_SD_ALPHA_N;
	engineConfiguration->isIgnitionEnabled = false;
	engineConfiguration->ignOverrideMode = AFR_AccPedal;
	auto inputs = consumerInputs();
	inputs.IgnitionOverride = AFR_AccPedal;
	inputs.Pedal = unexpected;
	ASSERT_TRUE(processAirmassConsumerLoads(inputs, 0.3f, true));
	engineConfiguration->gppwm[0].pin = Gpio::A0;
	engineConfiguration->gppwm[0].loadAxis = GPPWM_IgnLoad;
	EXPECT_TRUE(processAirmassConsumerLoads(inputs, 0.3f, false));
	EXPECT_FLOAT_EQ(readGppwmChannel(GPPWM_IgnLoad).value_or(0), 100);
	EXPECT_FALSE(hasAirmassLoadFallback());
	inputs.Pedal = 40;
	ASSERT_TRUE(processAirmassConsumerLoads(inputs, 0.3f, true));
	EXPECT_FLOAT_EQ(readGppwmChannel(GPPWM_IgnLoad).value_or(0), 40);
	inputs.Pedal = unexpected;
	engineConfiguration->gppwm[0].pin = Gpio::Unassigned;
	ASSERT_TRUE(processAirmassConsumerLoads(inputs, 0.3f, true));
}

TEST(AirmassConsumerSnapshot, CaptureChecksBurnVersionAndActiveStrategy) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->fuelAlgorithm = LM_SD_ALPHA_N;
	const auto inputs = consumerInputs();
	engine->globalConfigurationVersion++;
	EXPECT_FALSE(processAirmassConsumerLoads(inputs, 0.3f, true));
	EXPECT_FALSE(engine->engineState.airmassLoads.Valid);
	const auto beforeModeChange = consumerInputs();
	engineConfiguration->fuelAlgorithm = LM_ALPHA_N;
	EXPECT_FALSE(processAirmassConsumerLoads(beforeModeChange, 0.3f, true));
	EXPECT_FALSE(engine->engineState.airmassLoads.Valid);
}

TEST(AirmassConsumerSnapshot, ExternalModelEffectiveMapUsesLivePressureThenUpstreamDefault) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->fuelAlgorithm = LM_MOCK;
	Sensor::setMockValue(SensorType::Map, 40.125f);
	EXPECT_FLOAT_EQ(getAirmassSelectedLoad(AFR_EffectiveMAP, 0), 40.125f);
	EXPECT_FALSE(getEffectiveAirmassMap().Valid);
	Sensor::setInvalidMockValue(SensorType::Map);
	EXPECT_FLOAT_EQ(getAirmassSelectedLoad(AFR_EffectiveMAP, 0), 200);
	EXPECT_FALSE(getEffectiveAirmassMap().Valid);

	engineConfiguration->fuelAlgorithm = LM_SD_ALPHA_N;
	Sensor::setMockValue(SensorType::Map, 80);
	EXPECT_TRUE(std::isnan(getAirmassSelectedLoad(AFR_EffectiveMAP, 0)));
}

TEST(AirmassConsumerSnapshot, MafInvalidationDoesNotFallBackToLiveSensorsOrOldLoad) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->fuelAlgorithm = LM_REAL_MAF;
	Sensor::setMockValue(SensorType::Tps1, 75);
	engine->engineState.fuelingLoad = 50;
	engine->engineState.ignitionLoad = 60;
	config->injectionPhaseLoadSource = AFR_Tps;
	ASSERT_TRUE(processAirmassConsumerLoads(consumerInputs(), 0.3f, true));
	EXPECT_FLOAT_EQ(getAirmassConsumerLoad(AirmassConsumer::InjectionPhase), 20);
	invalidateAirmassLoads();
	EXPECT_TRUE(std::isnan(getAirmassConsumerLoad(AirmassConsumer::InjectionPhase)));
	EXPECT_FALSE(readGppwmChannel(GPPWM_FuelLoad).Valid);
	EXPECT_FALSE(readGppwmChannel(GPPWM_IgnLoad).Valid);
	EXPECT_FALSE(readGppwmChannel(GPPWM_EffectiveMap).Valid);
}

TEST(AirmassVeAnalyzeQualification, RequiresStartupDelayUniformAuthorityAndNoIdleTable) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->fuelAlgorithm = LM_SD_ALPHA_N;
	engineConfiguration->useSeparateVeForIdle = false;
	for (const float endpoint : {0.0f, 100.0f}) {
		setTable(config->airmassBlendTable, endpoint);
		engine->airmassInjectionState.onEngineStop();
		engine->fuelComputer.running.timeSinceCrankingInSecs = 9.9f;
		updateBlendedVeAnalyzeQualification(2000);
		EXPECT_EQ(engine->outputChannels.blendedVeAnalyzeEndpoint, 0);
		engine->fuelComputer.running.timeSinceCrankingInSecs = 10;
		updateBlendedVeAnalyzeQualification(2000);
		EXPECT_EQ(engine->outputChannels.blendedVeAnalyzeEndpoint, endpoint == 0 ? 1 : 2);
	}
	engine->airmassInjectionState.onEngineStop();
	config->airmassBlendTable[1][2] = 0;
	updateBlendedVeAnalyzeQualification(2000);
	EXPECT_EQ(engine->outputChannels.blendedVeAnalyzeEndpoint, 0);
	engine->airmassInjectionState.onEngineStop();
	setTable(config->airmassBlendTable, 50);
	updateBlendedVeAnalyzeQualification(2000);
	EXPECT_EQ(engine->outputChannels.blendedVeAnalyzeEndpoint, 0);
	engine->airmassInjectionState.onEngineStop();
	setTable(config->airmassBlendTable, 0);
	engineConfiguration->useSeparateVeForIdle = true;
	updateBlendedVeAnalyzeQualification(2000);
	EXPECT_EQ(engine->outputChannels.blendedVeAnalyzeEndpoint, 0);
}

TEST(AirmassVeAnalyzeQualification, MovingTuneWriteCannotRequalifyThroughZeroOrInvalidRpm) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->fuelAlgorithm = LM_SD_ALPHA_N;
	engineConfiguration->useSeparateVeForIdle = false;
	setTable(config->airmassBlendTable, 0);
	engine->airmassInjectionState.onEngineStop();
	engine->fuelComputer.running.timeSinceCrankingInSecs = 20;
	Sensor::setMockValue(SensorType::Rpm, 2000);
	updateBlendedVeAnalyzeQualification(2000);
	ASSERT_EQ(engine->outputChannels.blendedVeAnalyzeEndpoint, 1);
	engine->airmassInjectionState.onConfigurationWrite(LM_SD_ALPHA_N, false);
	setTable(config->airmassBlendTable, 100);
	for (float rpm : {2000.0f, 0.0f, NAN, 2000.0f}) {
		updateBlendedVeAnalyzeQualification(rpm);
		EXPECT_EQ(engine->outputChannels.blendedVeAnalyzeEndpoint, 0);
	}
	engine->airmassInjectionState.onEngineStop();
	updateBlendedVeAnalyzeQualification(2000);
	EXPECT_EQ(engine->outputChannels.blendedVeAnalyzeEndpoint, 2);
}

TEST(AirmassVeAnalyzeQualification, TunePreparedAtConfirmedStopCanQualifyAtNextStart) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->fuelAlgorithm = LM_SD_ALPHA_N;
	engineConfiguration->useSeparateVeForIdle = false;
	setTable(config->airmassBlendTable, 0);
	Sensor::setMockValue(SensorType::Rpm, 0);
	engine->airmassInjectionState.onConfigurationWrite(LM_SD_ALPHA_N, false);
	engine->fuelComputer.running.timeSinceCrankingInSecs = 20;
	updateBlendedVeAnalyzeQualification(2000);
	EXPECT_EQ(engine->outputChannels.blendedVeAnalyzeEndpoint, 1);
}

TEST_F(AirmassConsumers, HpfpMeasuredMapRetainsZeroFallbackWithoutChangingFuelOverrides) {
	config->hpfpTargetLoadSource = AFR_MAP;
	auto inputs = consumerInputs();
	inputs.MeasuredMap = unexpected;
	ASSERT_TRUE(processAirmassConsumerLoads(inputs, 0.2f, true));
	EXPECT_FLOAT_EQ(getAirmassConsumerLoad(AirmassConsumer::HpfpTarget), 0);
	EXPECT_FLOAT_EQ(getAirmassSelectedLoad(AFR_MAP, 0), 200);
	EXPECT_FALSE(engine->engineState.airmassLoads.ValidSources & (1u << AFR_MAP));
	inputs.MeasuredMap = 40;
	ASSERT_TRUE(processAirmassConsumerLoads(inputs, 0.2f, true));
	EXPECT_FLOAT_EQ(getAirmassConsumerLoad(AirmassConsumer::HpfpTarget), 40);
}
