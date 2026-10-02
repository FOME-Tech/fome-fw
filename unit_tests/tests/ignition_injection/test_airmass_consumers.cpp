#include "pch.h"

#include "airmass_loads.h"
#include "airmass.h"
#include "fuel_math.h"
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

TEST_F(AirmassConsumers, TwelveCylinderTrimsKeepIndependentMapsWithRepeatedAndMixedLoads) {
	setCylinderCount(12);
	Sensor::setMockValue(SensorType::AcceleratorPedal, 40);
	const load_override_e sources[] = {AFR_Tps, AFR_MAP, AFR_AccPedal, AFR_None, AFR_CylFilling, AFR_EffectiveMAP};
	for (size_t bin = 0; bin < TRIM_SIZE; bin++) {
		config->fuelTrimLoadBins[bin] = config->ignTrimLoadBins[bin] = bin * 50;
		config->fuelTrimRpmBins[bin] = config->ignTrimRpmBins[bin] = 500 + bin * 1500;
	}
	for (size_t cylinder = 0; cylinder < 12; cylinder++) {
		config->fuelTrimLoadSource[cylinder] = sources[cylinder % efi::size(sources)];
		config->ignitionTrimLoadSource[cylinder] = sources[(cylinder + 1) % efi::size(sources)];
		for (size_t row = 0; row < TRIM_SIZE; row++) {
			for (size_t column = 0; column < TRIM_SIZE; column++) {
				// Cylinder zero is a neutral reference; all other maps differ in both axes.
				float value = cylinder == 0
									? 0
									: (static_cast<int>((cylinder * 13 + row * 31 + column * 47) % 201) - 100) / 5.0f;
				config->fuelTrims[cylinder].table[row][column] = value;
				config->ignTrims[cylinder].table[row][column] = value;
			}
		}
	}
	const auto check = [&] {
		calculate();
		ASSERT_TRUE(engine->airmassInjectionState.allowInjection());
		const float untrimmedMass = engine->cylinders[0].getInjectionMass();
		const float untrimmedTiming = engine->cylinders[0].getIgnitionTimingBtdc();
		ASSERT_GT(untrimmedMass, 0);
		const float rpm = Sensor::getOrZero(SensorType::Rpm);
		for (size_t cylinder = 0; cylinder < 12; cylinder++) {
			const float fuelTrim = interpolate3d(
					config->fuelTrims[cylinder].table,
					config->fuelTrimLoadBins,
					getAirmassConsumerLoad(AirmassConsumer::FuelTrim, cylinder),
					config->fuelTrimRpmBins,
					rpm);
			const float ignitionTrim = interpolate3d(
					config->ignTrims[cylinder].table,
					config->ignTrimLoadBins,
					getAirmassConsumerLoad(AirmassConsumer::IgnitionTrim, cylinder),
					config->ignTrimRpmBins,
					rpm);
			EXPECT_FLOAT_EQ(engine->cylinders[cylinder].getInjectionMass(), untrimmedMass * ((100 + fuelTrim) / 100));
			EXPECT_FLOAT_EQ(engine->cylinders[cylinder].getIgnitionTimingBtdc(), untrimmedTiming + ignitionTrim);
		}
	};
	check();
	// The next calculation must observe changed axes, selectors, cells and inputs.
	config->fuelTrimLoadBins[1] = config->ignTrimLoadBins[1] = 35;
	config->fuelTrimRpmBins[1] = config->ignTrimRpmBins[1] = 2500;
	config->fuelTrims[7].table[1][1] = 23;
	config->ignTrims[9].table[2][1] = 19;
	config->fuelTrimLoadSource[7] = AFR_Tps;
	config->ignitionTrimLoadSource[9] = AFR_Tps;
	Sensor::setMockValue(SensorType::Tps1, 35);
	check();
	// The common single-coordinate case still samples each cylinder's own map.
	for (auto& source : config->fuelTrimLoadSource) {
		source = AFR_MAP;
	}
	for (auto& source : config->ignitionTrimLoadSource) {
		source = AFR_MAP;
	}
	check();
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

TEST(AirmassConsumerSnapshot, GettersHaveNoDiagnosticSideEffects) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->fuelAlgorithm = LM_SD_ALPHA_N;
	config->fuelTrimLoadSource[0] = AFR_Tps;
	config->ignitionTrimLoadSource[0] = AFR_MAP;
	config->knockGainLoadSource[0] = AFR_EffectiveMAP;
	auto inputs = consumerInputs();
	inputs.Tps = 20.125f;
	ASSERT_TRUE(processAirmassConsumerLoads(inputs, 0.3f, true));
	// A display update must belong to the explicit publisher, even when a
	// consumer is read after its cursor was changed by another diagnostic user.
	engine->outputChannels.fuelTrimLoad[0] = 11;
	engine->outputChannels.ignitionTrimLoad[0] = 12;
	engine->outputChannels.knockGainLoad[0] = 13;
	const AirmassConsumerLoadContext loads(AirmassConsumer::FuelTrim, AirmassConsumer::IgnitionTrim);
	EXPECT_FLOAT_EQ(getAirmassConsumerLoad(AirmassConsumer::FuelTrim), 20.125f);
	EXPECT_FLOAT_EQ(loads.get(AirmassConsumer::FuelTrim), 20.125f);
	EXPECT_FLOAT_EQ(getAirmassConsumerLoad(AirmassConsumer::IgnitionTrim), 80);
	EXPECT_FLOAT_EQ(getAirmassConsumerLoad(AirmassConsumer::KnockGain), 70);
	EXPECT_FLOAT_EQ(engine->outputChannels.fuelTrimLoad[0], 11);
	EXPECT_FLOAT_EQ(engine->outputChannels.ignitionTrimLoad[0], 12);
	EXPECT_FLOAT_EQ(engine->outputChannels.knockGainLoad[0], 13);
	invalidateAirmassLoads();
	EXPECT_FLOAT_EQ(engine->outputChannels.fuelTrimLoad[0], 0);
	engine->outputChannels.fuelTrimLoad[0] = 14;
	EXPECT_TRUE(std::isnan(getAirmassConsumerLoad(AirmassConsumer::FuelTrim)));
	EXPECT_FLOAT_EQ(engine->outputChannels.fuelTrimLoad[0], 14);
}

TEST(AirmassConsumerSnapshot, InvalidationClearsDiagnosticCursorsIncludingInactiveCylinders) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->enableSoftwareKnock = true;
	engineConfiguration->fuelAlgorithm = LM_SD_ALPHA_N;
	setCylinderCount(12);
	for (size_t cylinder = 0; cylinder < MAX_CYLINDER_COUNT; cylinder++) {
		config->fuelTrimLoadSource[cylinder] = AFR_Tps;
		config->ignitionTrimLoadSource[cylinder] = AFR_MAP;
		config->knockGainLoadSource[cylinder] = AFR_EffectiveMAP;
	}
	ASSERT_TRUE(processAirmassConsumerLoads(consumerInputs(), 0.3f, true));
	ASSERT_GT(engine->outputChannels.knockGainLoad[11], 0);
	setCylinderCount(4);
	invalidateAirmassConsumerLoads();
	EXPECT_FALSE(engine->engineState.airmassLoads.Valid);
	for (size_t cylinder = 0; cylinder < MAX_CYLINDER_COUNT; cylinder++) {
		EXPECT_FLOAT_EQ(engine->outputChannels.fuelTrimLoad[cylinder], 0);
		EXPECT_FLOAT_EQ(engine->outputChannels.ignitionTrimLoad[cylinder], 0);
		EXPECT_FLOAT_EQ(engine->outputChannels.knockGainLoad[cylinder], 0);
	}
	EXPECT_FLOAT_EQ(engine->outputChannels.injectionPhaseLoad, 0);
	EXPECT_FLOAT_EQ(engine->outputChannels.hpfpTargetLoad, 0);
	EXPECT_FLOAT_EQ(engine->outputChannels.trailingSparkLoad, 0);
}

TEST(AirmassConsumerSnapshot, ContextKeepsCoordinatesAndSelectorsCoherentWithinCalculation) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->fuelAlgorithm = LM_SD_ALPHA_N;
	config->fuelTrimLoadSource[0] = AFR_Tps;
	config->fuelTrimLoadSource[1] = AFR_MAP;
	config->ignitionTrimLoadSource[0] = AFR_EffectiveMAP;
	auto inputs = consumerInputs();
	inputs.Tps = 20.125f;
	ASSERT_TRUE(processAirmassConsumerLoads(inputs, 0.3f, true));
	const AirmassConsumerLoadContext loads(AirmassConsumer::FuelTrim, AirmassConsumer::IgnitionTrim);
	inputs.Tps = 35;
	inputs.MeasuredMap = 95;
	inputs.EffectiveMap.Map = 90;
	config->fuelTrimLoadSource[0] = AFR_MAP;
	ASSERT_TRUE(processAirmassConsumerLoads(inputs, 0.3f, true));
	EXPECT_FLOAT_EQ(loads.get(AirmassConsumer::FuelTrim, 0), 20.125f);
	EXPECT_FLOAT_EQ(loads.get(AirmassConsumer::FuelTrim, 1), 80);
	EXPECT_FLOAT_EQ(loads.get(AirmassConsumer::IgnitionTrim, 0), 70);
	const AirmassConsumerLoadContext next(AirmassConsumer::FuelTrim);
	EXPECT_FLOAT_EQ(next.get(AirmassConsumer::FuelTrim), 95);
	EXPECT_TRUE(std::isnan(next.get(AirmassConsumer::IgnitionTrim)));
	EXPECT_TRUE(std::isnan(next.get(AirmassConsumer::FuelTrim, MAX_CYLINDER_COUNT)));
	EXPECT_TRUE(std::isnan(next.get(AirmassConsumer::Count)));
}

TEST(AirmassConsumerSnapshot, NewContextsRejectInvalidatedOrVersionStalePublications) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->fuelAlgorithm = LM_SD_ALPHA_N;
	config->fuelTrimLoadSource[0] = AFR_Tps;
	for (const bool tuneWrite : {false, true}) {
		SCOPED_TRACE(tuneWrite);
		const auto token = engine->airmassInjectionState.beginCalculation(
				LM_SD_ALPHA_N, 2000, engine->getGlobalConfigurationVersion());
		ASSERT_TRUE(processAirmassConsumerLoads(consumerInputs(), 0.3f, true));
		const AirmassConsumerLoadContext old(AirmassConsumer::FuelTrim);
		EXPECT_FLOAT_EQ(old.get(AirmassConsumer::FuelTrim), 20);
		if (tuneWrite) {
			engine->airmassInjectionState.onConfigurationWrite(LM_SD_ALPHA_N, false);
		} else {
			engine->airmassInjectionState.onEngineStop();
		}
		const AirmassConsumerLoadContext invalid(AirmassConsumer::FuelTrim);
		EXPECT_TRUE(std::isnan(invalid.get(AirmassConsumer::FuelTrim)));
		// A local captured result cannot authorize a fuel commit after invalidation.
		EXPECT_FALSE(engine->airmassInjectionState.isCalculationCurrent(token));
		engine->airmassInjectionState.completeCalculation(token, true);
		EXPECT_FALSE(engine->airmassInjectionState.allowInjection());
	}
	ASSERT_TRUE(processAirmassConsumerLoads(consumerInputs(), 0.3f, true));
	engine->globalConfigurationVersion++;
	const AirmassConsumerLoadContext stale(AirmassConsumer::FuelTrim);
	EXPECT_TRUE(std::isnan(stale.get(AirmassConsumer::FuelTrim)));
}

TEST(AirmassConsumerSnapshot, LegacyPublicationOwnsCursorsAndPreservesPhysicalFallbacks) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE, [](engine_configuration_s* cfg) { cfg->hpfpValvePin = Gpio::A2; });
	engineConfiguration->enableSoftwareKnock = true;
	engineConfiguration->hpfpCamLobes = 3;
	engineConfiguration->hpfpPumpVolume = 0.2f;
	Sensor::setMockValue(SensorType::Rpm, 2000);
	engineConfiguration->fuelAlgorithm = LM_MOCK;
	setCylinderCount(12);
	engine->engineState.fuelingLoad = 60.125f;
	engine->fuelComputer.normalizedCylinderFilling = 75.125f;
	Sensor::setMockValue(SensorType::Map, 80.125f);
	Sensor::setMockValue(SensorType::Tps1, 20.125f);
	config->injectionPhaseLoadSource = AFR_None;
	config->fuelTrimLoadSource[11] = AFR_Tps;
	config->ignitionTrimLoadSource[11] = AFR_CylFilling;
	config->knockGainLoadSource[11] = AFR_EffectiveMAP;
	config->hpfpTargetLoadSource = AFR_MAP;
	engine->outputChannels.injectionPhaseLoad = 12;
	EXPECT_FLOAT_EQ(getAirmassConsumerLoad(AirmassConsumer::InjectionPhase), 60.125f);
	EXPECT_FLOAT_EQ(engine->outputChannels.injectionPhaseLoad, 12);
	publishLegacyAirmassConsumerLoads();
	EXPECT_NEAR(engine->outputChannels.injectionPhaseLoad, 60.1f, 0.01f);
	EXPECT_NEAR(engine->outputChannels.fuelTrimLoad[11], 20.1f, 0.01f);
	EXPECT_NEAR(engine->outputChannels.ignitionTrimLoad[11], 75.1f, 0.01f);
	EXPECT_NEAR(engine->outputChannels.knockGainLoad[11], 80.1f, 0.01f);
	EXPECT_NEAR(engine->outputChannels.hpfpTargetLoad, 80.1f, 0.01f);
	const AirmassConsumerLoadContext captured(AirmassConsumer::FuelTrim, AirmassConsumer::HpfpTarget);
	Sensor::setInvalidMockValue(SensorType::Map);
	Sensor::setInvalidMockValue(SensorType::Tps1);
	EXPECT_FLOAT_EQ(captured.get(AirmassConsumer::FuelTrim, 11), 20.125f);
	EXPECT_FLOAT_EQ(captured.get(AirmassConsumer::HpfpTarget), 80.125f);
	const AirmassConsumerLoadContext missing(AirmassConsumer::FuelTrim, AirmassConsumer::HpfpTarget);
	EXPECT_FLOAT_EQ(missing.get(AirmassConsumer::FuelTrim, 11), 100);
	EXPECT_FLOAT_EQ(missing.get(AirmassConsumer::HpfpTarget), 0);
	publishLegacyAirmassConsumerLoads();
	EXPECT_FLOAT_EQ(engine->outputChannels.fuelTrimLoad[11], 100);
	EXPECT_FLOAT_EQ(engine->outputChannels.knockGainLoad[11], 200);
	EXPECT_FLOAT_EQ(engine->outputChannels.hpfpTargetLoad, 0);
	engineConfiguration->fuelAlgorithm = LM_SD_ALPHA_N;
	engine->outputChannels.fuelTrimLoad[11] = 15;
	publishLegacyAirmassConsumerLoads();
	EXPECT_FLOAT_EQ(engine->outputChannels.fuelTrimLoad[11], 15);
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

TEST(AirmassConsumerSnapshot, PackedPublicationPreservesPrecisionAndClearsUnusedSlots) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->fuelAlgorithm = LM_SD_ALPHA_N;
	setCylinderCount(12);
	config->injectionPhaseLoadSource = AFR_Tps;
	config->fuelTrimLoadSource[11] = AFR_CylFilling;
	config->ignitionTrimLoadSource[11] = static_cast<load_override_e>(255);
	auto inputs = consumerInputs();
	inputs.Tps = 20.125f;
	ASSERT_TRUE(processAirmassConsumerLoads(inputs, 100, true));
	EXPECT_FLOAT_EQ(getAirmassConsumerLoad(AirmassConsumer::InjectionPhase), 20.125f);
	EXPECT_NEAR(engine->outputChannels.injectionPhaseLoad, 20.1f, 0.001f);
	EXPECT_GT(getAirmassConsumerLoad(AirmassConsumer::FuelTrim, 11), 3276);
	EXPECT_FLOAT_EQ(engine->outputChannels.fuelTrimLoad[11], 3276);
	EXPECT_FLOAT_EQ(engine->outputChannels.ignitionTrimLoad[11], 0);
	setCylinderCount(4);
	ASSERT_TRUE(processAirmassConsumerLoads(consumerInputs(), 0.3f, true));
	for (size_t cylinder = 4; cylinder < MAX_CYLINDER_COUNT; cylinder++) {
		EXPECT_FLOAT_EQ(engine->outputChannels.fuelTrimLoad[cylinder], 0);
		EXPECT_FLOAT_EQ(engine->outputChannels.ignitionTrimLoad[cylinder], 0);
		EXPECT_FLOAT_EQ(engine->outputChannels.knockGainLoad[cylinder], 0);
	}
	EXPECT_FALSE(processAirmassConsumerLoads(consumerInputs(), NAN, true));
	EXPECT_FALSE(engine->engineState.airmassLoads.Valid);
	EXPECT_FLOAT_EQ(engine->outputChannels.injectionPhaseLoad, 0);
	EXPECT_FLOAT_EQ(engine->outputChannels.fuelTrimLoad[0], 0);
}

TEST(AirmassConsumerSnapshot, LegacyPackingClampsAndHandlesNonfiniteCoordinates) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->fuelAlgorithm = LM_MOCK;
	config->injectionPhaseLoadSource = AFR_None;
	const float loads[] = {20.125f, 20.25f, -20.125f, -20.25f, 4000, -4000, INFINITY, -INFINITY, NAN};
	const float expected[] = {20.1f, 20.3f, -20.1f, -20.3f, 3276, -3276, 0, 0, 0};
	for (size_t i = 0; i < efi::size(loads); i++) {
		SCOPED_TRACE(i);
		engine->engineState.fuelingLoad = loads[i];
		publishLegacyAirmassConsumerLoads();
		EXPECT_NEAR(engine->outputChannels.injectionPhaseLoad, expected[i], 0.001f);
	}
}

TEST(AirmassConsumerSnapshot, PreparedPhysicalAndLegacyCursorsRejectInterveningInvalidation) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	config->injectionPhaseLoadSource = AFR_Tps;
	Sensor::setMockValue(SensorType::Tps1, 90);
	for (const auto strategy : {LM_SD_ALPHA_N, LM_MOCK}) {
		for (int change = 0; change < 4; change++) {
			SCOPED_TRACE(strategy);
			SCOPED_TRACE(change);
			engineConfiguration->fuelAlgorithm = strategy;
			engine->airmassInjectionState.beginCalculation(strategy, 2000, engine->getGlobalConfigurationVersion());
			auto inputs = consumerInputs();
			inputs.Tps = 90;
			bool prepared = false;
			engine->onAirmassConsumerLoadsPrepared = [&] {
				prepared = true;
				if (change == 0) {
					engine->airmassInjectionState.onEngineStop();
				} else if (change == 1) {
					engine->globalConfigurationVersion++;
					engine->airmassInjectionState.onConfigurationWrite(strategy, false);
				} else if (change == 2) {
					engineConfiguration->fuelAlgorithm = strategy == LM_MOCK ? LM_SD_ALPHA_N : LM_MOCK;
				} else {
					engine->airmassInjectionState.beginCalculation(
							strategy, 2000, engine->getGlobalConfigurationVersion());
				}
				engine->outputChannels.injectionPhaseLoad = 35;
			};
			if (strategy == LM_MOCK) {
				publishLegacyAirmassConsumerLoads();
			} else {
				EXPECT_FALSE(processAirmassConsumerLoads(inputs, 0.3f, true));
			}
			engine->onAirmassConsumerLoadsPrepared = nullptr;
			EXPECT_TRUE(prepared);
			EXPECT_FLOAT_EQ(engine->outputChannels.injectionPhaseLoad, 35);
		}
	}
}

TEST_F(AirmassConsumers, FinalFuelRejectsAllCylindersForInvalidBankOrResult) {
	for (int failure = 0; failure < 4; failure++) {
		SCOPED_TRACE(failure);
		engineConfiguration->cylinderBankSelect[3] = 0;
		engine->engineState.lua.fuelAdd = 0;
		calculate();
		ASSERT_TRUE(engine->airmassInjectionState.allowInjection());
		float previousMasses[4];
		for (size_t cylinder = 0; cylinder < 4; cylinder++) {
			previousMasses[cylinder] = engine->cylinders[cylinder].getInjectionMass();
			ASSERT_GT(previousMasses[cylinder], 0);
		}
		if (failure == 0) {
			engineConfiguration->cylinderBankSelect[3] = STFT_BANK_COUNT;
		} else {
			const float invalid[] = {NAN, INFINITY, -100};
			engine->engineState.lua.fuelAdd = invalid[failure - 1];
		}
		bool prepared = false;
		engine->onCylinderFuelPrepared = [&] {
			prepared = true;
			// Preparation cannot expose even a partial cylinder update.
			for (size_t cylinder = 0; cylinder < 4; cylinder++) {
				EXPECT_FLOAT_EQ(engine->cylinders[cylinder].getInjectionMass(), previousMasses[cylinder]);
			}
		};
		calculate();
		engine->onCylinderFuelPrepared = nullptr;
		EXPECT_TRUE(prepared);
		EXPECT_FALSE(engine->airmassInjectionState.allowInjection());
		EXPECT_FLOAT_EQ(engine->engineState.injectionDuration, 0);
		EXPECT_FLOAT_EQ(engine->engineState.injectionDurationStage2, 0);
		for (const auto& cylinder : engine->cylinders) {
			EXPECT_FLOAT_EQ(cylinder.getInjectionMass(), 0);
		}
	}
}

TEST_F(AirmassConsumers, PreparedFinalFuelAndTimingRejectStopTuneAndNewerCalculation) {
	for (int change = 0; change < 3; change++) {
		SCOPED_TRACE(change);
		calculate();
		ASSERT_TRUE(engine->airmassInjectionState.allowInjection());
		bool prepared = false;
		engine->onCylinderFuelPrepared = [&] {
			prepared = true;
			if (change == 0) {
				engine->airmassInjectionState.onEngineStop();
			} else if (change == 1) {
				engine->globalConfigurationVersion++;
				engine->airmassInjectionState.onConfigurationWrite(LM_SD_ALPHA_N, false);
			} else {
				engine->airmassInjectionState.beginCalculation(
						LM_SD_ALPHA_N, 2000, engine->getGlobalConfigurationVersion());
			}
			// Distinguishable newer values must survive every part of a stale commit.
			engine->engineState.injectionDuration = 123;
			engine->engineState.shouldUpdateInjectionTiming = false;
			for (auto& cylinder : engine->cylinders) {
				cylinder.setInjectionMass(0);
				cylinder.setIgnitionTimingBtdc(77);
			}
		};
		calculate();
		engine->onCylinderFuelPrepared = nullptr;
		EXPECT_TRUE(prepared);
		EXPECT_FLOAT_EQ(engine->engineState.injectionDuration, 123);
		EXPECT_FALSE(engine->engineState.shouldUpdateInjectionTiming);
		for (size_t cylinder = 0; cylinder < 4; cylinder++) {
			EXPECT_FLOAT_EQ(engine->cylinders[cylinder].getInjectionMass(), 0);
			EXPECT_FLOAT_EQ(engine->cylinders[cylinder].getIgnitionTimingBtdc(), 77);
		}
	}
}

TEST_F(AirmassConsumers, LegacyInvalidFuelClearsEveryCylinderAndValidCalculationRecovers) {
	engineConfiguration->fuelAlgorithm = LM_MOCK;
	EXPECT_CALL(*eth.mockAirmass, getAirmass(testing::_, testing::_))
			.WillRepeatedly(testing::Return(AirmassResult{0.3f, 70}));
	calculate();
	ASSERT_GT(engine->cylinders[0].getInjectionMass(), 0);
	engineConfiguration->cylinderBankSelect[3] = STFT_BANK_COUNT;
	calculate();
	EXPECT_FLOAT_EQ(engine->engineState.injectionDuration, 0);
	for (const auto& cylinder : engine->cylinders) {
		EXPECT_FLOAT_EQ(cylinder.getInjectionMass(), 0);
	}
	engineConfiguration->cylinderBankSelect[3] = 0;
	calculate();
	ASSERT_GT(engine->cylinders[0].getInjectionMass(), 0);
	engine->engineState.lua.fuelAdd = INFINITY;
	calculate();
	EXPECT_FLOAT_EQ(engine->engineState.injectionDuration, 0);
	for (const auto& cylinder : engine->cylinders) {
		EXPECT_FLOAT_EQ(cylinder.getInjectionMass(), 0);
	}
	engine->engineState.lua.fuelAdd = 0;
	calculate();
	EXPECT_GT(engine->cylinders[0].getInjectionMass(), 0);
}

TEST_F(AirmassConsumers, DisabledInjectionPublishesZeroFuelAndKeepsIndependentSpark) {
	calculate();
	ASSERT_GT(engine->cylinders[0].getInjectionMass(), 0);
	engineConfiguration->isInjectionEnabled = false;
	calculate();
	EXPECT_FLOAT_EQ(engine->engineState.injectionDuration, 0);
	EXPECT_FLOAT_EQ(engine->engineState.injectionDurationStage2, 0);
	EXPECT_TRUE(engine->engineState.shouldUpdateInjectionTiming);
	for (size_t cylinder = 0; cylinder < 4; cylinder++) {
		EXPECT_FLOAT_EQ(engine->cylinders[cylinder].getInjectionMass(), 0);
		EXPECT_TRUE(std::isfinite(engine->cylinders[cylinder].getIgnitionTimingBtdc()));
	}
	EXPECT_GT(engine->cylinders[1].getIgnitionTimingBtdc(), engine->cylinders[0].getIgnitionTimingBtdc() + 5);
}

TEST_F(AirmassConsumers, InjectionTimingDutyUsesPreparedDurationWithOriginalRounding) {
	for (const auto mode : {IM_SEQUENTIAL, IM_BATCH, IM_SIMULTANEOUS}) {
		SCOPED_TRACE(mode);
		engineConfiguration->injectionMode = mode;
		engineConfiguration->crankingInjectionMode = mode;
		for (const float add : {0.0f, 1.0f}) {
			SCOPED_TRACE(add);
			engine->engineState.lua.fuelAdd = add;
			// The previous published duration must have no effect on this calculation's decision.
			engine->engineState.injectionDuration = 1000;
			calculate();
			ASSERT_TRUE(engine->airmassInjectionState.allowInjection());
			EXPECT_EQ(engine->engineState.shouldUpdateInjectionTiming, getInjectorDutyCycle(2000) < 90);
		}
	}
}

TEST_F(AirmassConsumers, DisabledOptionalFeaturesSkipControlReadsAndClearCursors) {
	engineConfiguration->enableStagedInjection = true;
	engineConfiguration->enableSoftwareKnock = true;
	engineConfiguration->lambdaProtectionEnable = true;
	calculate();
	ASSERT_GT(engine->outputChannels.trailingSparkLoad, 0);
	ASSERT_GT(engine->outputChannels.knockGainLoad[0], 0);
	engineConfiguration->enableStagedInjection = false;
	engineConfiguration->enableTrailingSparks = false;
	engineConfiguration->enableSoftwareKnock = false;
	engineConfiguration->fuelClosedLoopCorrectionEnabled = false;
	engineConfiguration->lambdaProtectionEnable = false;
	config->stagingLoadSource = config->trailingSparkLoadSource = config->lambdaMonitorLoadSource =
			static_cast<load_override_e>(255);
	config->injectorStagingRpmBins[1] = config->injectorStagingRpmBins[0];
	config->trailingIgnitionRpmBins[1] = config->trailingIgnitionRpmBins[0];
	resetAirmassCursorPreparationCounts();
	eth.moveTimeForwardMs(1000);
	calculate();
	EXPECT_TRUE(engine->engineState.airmassCalculationValid);
	EXPECT_GT(engine->cylinders[0].getInjectionMass(), 0);
	EXPECT_FLOAT_EQ(engine->engineState.injectionStage2Fraction, 0);
	EXPECT_FLOAT_EQ(engine->engineState.trailingSparkAngle, 0);
	EXPECT_TRUE(engine->lambdaMonitor.lambdaCurrentlyGood);
	EXPECT_FLOAT_EQ(engine->lambdaMonitor.lambdaTimeSinceGood, 0);
	for (auto consumer :
		 {AirmassConsumer::Staging,
		  AirmassConsumer::TrailingSpark,
		  AirmassConsumer::LambdaMonitor,
		  AirmassConsumer::KnockGain,
		  AirmassConsumer::KnockRetard}) {
		EXPECT_EQ(getAirmassConsumerReadCount(consumer), 0u);
		EXPECT_EQ(getAirmassCursorPreparationCount(consumer), 0u);
	}
	EXPECT_FLOAT_EQ(engine->outputChannels.stagingLoad, 0);
	EXPECT_FLOAT_EQ(engine->outputChannels.trailingSparkLoad, 0);
	EXPECT_FLOAT_EQ(engine->outputChannels.knockGainLoad[0], 0);
	EXPECT_FLOAT_EQ(engine->outputChannels.lambdaMonitorLoad, 0);
	// Both disabled trim families also avoid their selectors and RPM bin work.
	engineConfiguration->isInjectionEnabled = false;
	engineConfiguration->isIgnitionEnabled = false;
	config->fuelTrimLoadSource[0] = config->ignitionTrimLoadSource[0] = static_cast<load_override_e>(255);
	resetAirmassCursorPreparationCounts();
	calculate();
	EXPECT_EQ(getAirmassConsumerReadCount(AirmassConsumer::FuelTrim), 0u);
	EXPECT_EQ(getAirmassConsumerReadCount(AirmassConsumer::IgnitionTrim), 0u);
	EXPECT_FLOAT_EQ(engine->outputChannels.fuelTrimLoad[0], 0);
	EXPECT_FLOAT_EQ(engine->outputChannels.ignitionTrimLoad[0], 0);
}

TEST_F(AirmassConsumers, DisabledLambdaMonitorRetainsRestoreLoadUntilLatchedCutClears) {
	engineConfiguration->lambdaProtectionEnable = false;
	engineConfiguration->lambdaProtectionRestoreRpm = 2500;
	engineConfiguration->lambdaProtectionRestoreLoad = 50;
	engineConfiguration->lambdaProtectionRestoreTps = 30;
	config->lambdaMonitorLoadSource = AFR_MAP;
	engine->lambdaMonitor.lambdaMonitorCut = true;
	resetAirmassCursorPreparationCounts();
	calculate();
	EXPECT_TRUE(engine->lambdaMonitor.isCut());
	EXPECT_TRUE(engine->lambdaMonitor.lambdaCurrentlyGood);
	EXPECT_EQ(getAirmassConsumerReadCount(AirmassConsumer::LambdaMonitor), 1u);
	EXPECT_EQ(getAirmassCursorPreparationCount(AirmassConsumer::LambdaMonitor), 1u);
	EXPECT_FLOAT_EQ(engine->outputChannels.lambdaMonitorLoad, 80);
	Sensor::setMockValue(SensorType::Map, 40);
	calculate();
	EXPECT_FALSE(engine->lambdaMonitor.isCut());
	EXPECT_FLOAT_EQ(engine->outputChannels.lambdaMonitorLoad, 40);
	resetAirmassCursorPreparationCounts();
	calculate();
	EXPECT_EQ(getAirmassConsumerReadCount(AirmassConsumer::LambdaMonitor), 0u);
	EXPECT_EQ(getAirmassCursorPreparationCount(AirmassConsumer::LambdaMonitor), 0u);
	EXPECT_FLOAT_EQ(engine->outputChannels.lambdaMonitorLoad, 0);
}

TEST(AirmassConsumerSnapshot, DisabledV12OptionalCursorsSkipNineteenPreparationsForPhysicalAndLegacyModels) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setCylinderCount(12);
	engineConfiguration->isInjectionEnabled = true;
	engineConfiguration->isIgnitionEnabled = true;
	engineConfiguration->enableStagedInjection = false;
	engineConfiguration->enableTrailingSparks = false;
	engineConfiguration->enableSoftwareKnock = false;
	engineConfiguration->fuelClosedLoopCorrectionEnabled = false;
	engineConfiguration->lambdaProtectionEnable = false;
	config->knockRetardLoadSource = AFR_EffectiveMAP;
	for (auto& source : config->knockGainLoadSource) {
		source = AFR_EffectiveMAP;
	}
	for (auto strategy : {LM_SD_ALPHA_N, LM_MOCK}) {
		engineConfiguration->fuelAlgorithm = strategy;
		resetAirmassCursorPreparationCounts();
		if (strategy == LM_MOCK) {
			engine->engineState.fuelingLoad = 70;
			Sensor::setMockValue(SensorType::Map, 70);
			publishLegacyAirmassConsumerLoads();
		} else {
			ASSERT_TRUE(processAirmassConsumerLoads(consumerInputs(), 0.3f, true));
		}
		unsigned total = 0;
		for (unsigned i = 0; i < static_cast<unsigned>(AirmassConsumer::Count); i++) {
			total += getAirmassCursorPreparationCount(static_cast<AirmassConsumer>(i));
		}
		EXPECT_EQ(total, 26u);
		EXPECT_EQ(getAirmassCursorPreparationCount(AirmassConsumer::KnockGain), 0u);
		EXPECT_EQ(getAirmassCursorPreparationCount(AirmassConsumer::KnockRetard), 0u);
		EXPECT_EQ(getAirmassCursorPreparationCount(AirmassConsumer::HpfpTarget), 0u);
		EXPECT_FLOAT_EQ(engine->outputChannels.knockGainLoad[11], 0);
		EXPECT_FLOAT_EQ(engine->outputChannels.knockRetardLoad, 0);
		// Read-only coordinate inspection retains full precision while disabled.
		EXPECT_FLOAT_EQ(getAirmassConsumerLoad(AirmassConsumer::KnockGain, 11), 70);
		engineConfiguration->enableSoftwareKnock = true;
		if (strategy == LM_MOCK) {
			publishLegacyAirmassConsumerLoads();
		} else {
			ASSERT_TRUE(processAirmassConsumerLoads(consumerInputs(), 0.3f, true));
		}
		EXPECT_FLOAT_EQ(engine->outputChannels.knockGainLoad[11], 70);
		engineConfiguration->enableSoftwareKnock = false;
	}
}
