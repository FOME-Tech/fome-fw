#include "pch.h"
#include "airmass_loads.h"

struct ConsumerLambdaMonitor : public LambdaMonitor {
	using LambdaMonitorBase::isCurrentlyGood;
	using LambdaMonitorBase::restoreConditionsMet;
};

struct FixedThresholdLambdaMonitor : public LambdaMonitor {
	using LambdaMonitorBase::isCurrentlyGood;
	using LambdaMonitorBase::restoreConditionsMet;

	float getMaxAllowedLambda(float /*rpm*/, float /*load*/) const override {
		return 1.1f;
	}
};

TEST(LambdaMonitor, StoppedNaNLoadDoesNotCutSpeedDensityOrBlendedModels) {
	for (auto mode : {LM_SPEED_DENSITY, LM_SD_ALPHA_N}) {
		SCOPED_TRACE(static_cast<int>(mode));
		EngineTestHelper eth(engine_type_e::TEST_ENGINE);
		engineConfiguration->fuelAlgorithm = mode;
		engineConfiguration->isInjectionEnabled = true;
		engineConfiguration->lambdaProtectionEnable = true;
		engineConfiguration->lambdaProtectionMinRpm = 1000;
		engineConfiguration->lambdaProtectionMinLoad = 50;
		engineConfiguration->lambdaProtectionMinTps = 0;
		engineConfiguration->lambdaProtectionRestoreRpm = 2000;
		engineConfiguration->lambdaProtectionRestoreLoad = 30;
		engineConfiguration->lambdaProtectionRestoreTps = 20;
		engineConfiguration->lambdaProtectionTimeout = 0.5f;
		engineConfiguration->useSeparateVeForIdle = false;
		setTable(config->airmassBlendTable, 0);
		setTable(config->lambdaTable, 1);
		Sensor::setMockValue(SensorType::Map, 95);
		Sensor::setMockValue(SensorType::Tps1, 0);
		Sensor::setMockValue(SensorType::Iat, 20);
		Sensor::setMockValue(SensorType::Clt, 80);
		engine->rpmCalculator.setRpmValue(0);
		engine->engineState.periodicFastCallback();
		advanceTimeUs(600000);
		engine->engineState.periodicFastCallback();
		EXPECT_FALSE(engine->lambdaMonitor.isCut()) << "lambda cut while stopped";

		engine->rpmCalculator.setRpmValue(300);
		engine->engineState.periodicFastCallback();
		ASSERT_TRUE(engine->engineState.airmassCalculationValid);
		ASSERT_TRUE(engine->airmassInjectionState.allowInjection());
		EXPECT_FALSE(engine->lambdaMonitor.isCut()) << "lambda cut remains while cranking at 95 kPa";
		engine->module<LimpManager>()->onFastCallback();
		EXPECT_TRUE(engine->module<LimpManager>()->allowInjection().value);
	}
}

TEST(LambdaMonitor, DeviationTableAndThresholdsHaveIndependentCoordinates) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setTimeNowUs(10e6);
	Sensor::setMockValue(SensorType::Map, 80);
	Sensor::setMockValue(SensorType::Tps1, 20);
	Sensor::setMockValue(SensorType::Lambda1, 1.10f);
	engineConfiguration->lambdaProtectionEnable = true;
	engineConfiguration->lambdaProtectionMinRpm = 1000;
	engineConfiguration->lambdaProtectionMinLoad = 50;
	engineConfiguration->lambdaProtectionMinTps = 0;
	engineConfiguration->lambdaProtectionRestoreRpm = 3000;
	engineConfiguration->lambdaProtectionRestoreLoad = 50;
	engineConfiguration->lambdaProtectionRestoreTps = 100;
	engine->fuelComputer.targetLambda = 1;
	config->lambdaMonitorLoadSource = AFR_MAP;
	config->lambdaDeviationLoadSource = AFR_Tps;
	setLinearCurve(config->lambdaMaxDeviationLoadBins, 0, 100, 1);
	for (size_t row = 0; row < efi::size(config->lambdaMaxDeviationTable); row++) {
		setArrayValues(config->lambdaMaxDeviationTable[row], 0.002f * config->lambdaMaxDeviationLoadBins[row]);
	}
	ConsumerLambdaMonitor monitor;
	const auto controlLoad = [] { return getAirmassConsumerLoad(AirmassConsumer::LambdaMonitor); };
	EXPECT_FALSE(monitor.isCurrentlyGood(2000, controlLoad()));
	EXPECT_FALSE(monitor.restoreConditionsMet(2000, controlLoad()));

	config->lambdaDeviationLoadSource = AFR_MAP;
	EXPECT_TRUE(monitor.isCurrentlyGood(2000, controlLoad()));
	EXPECT_FALSE(monitor.restoreConditionsMet(2000, controlLoad()));

	config->lambdaDeviationLoadSource = AFR_Tps;
	config->lambdaMonitorLoadSource = AFR_Tps;
	EXPECT_TRUE(monitor.isCurrentlyGood(2000, controlLoad()));
	EXPECT_TRUE(monitor.restoreConditionsMet(2000, controlLoad()));
	EXPECT_FALSE(monitor.isCurrentlyGood(2000, NAN));
	EXPECT_FALSE(monitor.restoreConditionsMet(2000, NAN));
	EXPECT_TRUE(monitor.isCurrentlyGood(500, NAN));
	EXPECT_FALSE(monitor.restoreConditionsMet(500, NAN));
}

TEST(LambdaMonitor, RunningFaultNeedsConfiguredRestoreOrConfirmedStop) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	Sensor::setMockValue(SensorType::Tps1, 20);
	Sensor::setMockValue(SensorType::Lambda1, 1.0f);
	engineConfiguration->lambdaProtectionEnable = true;
	engineConfiguration->lambdaProtectionMinRpm = 1000;
	engineConfiguration->lambdaProtectionMinLoad = 50;
	engineConfiguration->lambdaProtectionMinTps = 0;
	engineConfiguration->lambdaProtectionRestoreRpm = 2000;
	engineConfiguration->lambdaProtectionRestoreLoad = 30;
	engineConfiguration->lambdaProtectionRestoreTps = 20;
	engineConfiguration->lambdaProtectionTimeout = 0.5f;
	engineConfiguration->noFuelTrimAfterDfcoTime = 0;
	engine->rpmCalculator.setRpmValue(2000);
	// The 0-to-running transition invokes a fast callback that resets LimpManager's recent-cut timer.
	setTimeNowUs(10e6);

	FixedThresholdLambdaMonitor monitor;
	monitor.update(2000, 95);
	EXPECT_FALSE(monitor.isCut());

	Sensor::setMockValue(SensorType::Lambda1, 1.2f);
	monitor.update(2000, 95);
	EXPECT_FALSE(monitor.isCut());
	advanceTimeUs(600000);
	monitor.update(2000, 95);
	ASSERT_TRUE(monitor.isCut());

	// A healthy lambda reading cannot restore fuel while running above the configured restore load.
	Sensor::setMockValue(SensorType::Lambda1, 1.0f);
	monitor.update(2000, 95);
	EXPECT_TRUE(monitor.isCut());

	// Invalid load at running RPM remains a fault and cannot restore the cut.
	monitor.update(2000, NAN);
	EXPECT_FALSE(monitor.lambdaCurrentlyGood);
	EXPECT_FALSE(monitor.restoreConditionsMet(2000, NAN));
	EXPECT_TRUE(monitor.isCut());

	// A zero RPM sample cannot clear the cut while the calculator still reports a running engine.
	Sensor::setMockValue(SensorType::Rpm, 0);
	monitor.update(0, NAN);
	EXPECT_TRUE(monitor.isCut());

	// The normal configured restore window still clears a running cut.
	monitor.update(1500, 30);
	EXPECT_FALSE(monitor.isCut());

	// A later cut is cleared after the ECU confirms a real stop, even if load is unavailable.
	Sensor::setMockValue(SensorType::Lambda1, 1.2f);
	monitor.update(2000, 95);
	EXPECT_FALSE(monitor.isCut());
	advanceTimeUs(600000);
	monitor.update(2000, 95);
	ASSERT_TRUE(monitor.isCut());
	engine->rpmCalculator.setRpmValue(0);
	Sensor::setMockValue(SensorType::Rpm, 0);
	monitor.update(0, NAN);
	EXPECT_FALSE(monitor.isCut());
	monitor.update(300, 95);
	EXPECT_FALSE(monitor.isCut());
}

struct MockLambdaMonitor : public LambdaMonitorBase {
	bool isGood = true;
	bool isCurrentlyGood(float /*rpm*/, float /*load*/) const override {
		return isGood;
	}

	bool isRestore = false;
	bool restoreConditionsMet(float /*rpm*/, float /*load*/) const override {
		return isRestore;
	}

	float getTimeout() const override {
		// Timeout of 1 second
		return 1;
	}

	MOCK_METHOD(float, getMaxAllowedLambda, (float rpm, float load), (const, override));
};

TEST(LambdaMonitor, Response) {
	MockLambdaMonitor mlm;

	int startTime = 1e6;
	setTimeNowUs(startTime);

	mlm.isGood = true;
	mlm.isRestore = false;
	mlm.update(2000, 50);
	EXPECT_TRUE(mlm.lambdaCurrentlyGood);
	EXPECT_FALSE(mlm.isCut());

	// now lambda will be bad, but we don't cut yet
	mlm.isGood = false;
	mlm.update(2000, 50);
	EXPECT_FALSE(mlm.lambdaCurrentlyGood);
	EXPECT_FALSE(mlm.isCut());

	// 0.9 second later, still not cut
	setTimeNowUs(startTime + 0.9e6);
	mlm.update(2000, 50);
	EXPECT_FALSE(mlm.lambdaCurrentlyGood);
	EXPECT_FALSE(mlm.isCut());

	// 1.1 second later, cut!
	setTimeNowUs(startTime + 1.1e6);
	mlm.update(2000, 50);
	EXPECT_FALSE(mlm.lambdaCurrentlyGood);
	EXPECT_TRUE(mlm.isCut());

	// Lambda goes back to normal, but restore conditions not met so we should stay cut
	mlm.isGood = true;
	mlm.update(2000, 50);
	EXPECT_TRUE(mlm.lambdaCurrentlyGood);
	EXPECT_TRUE(mlm.isCut());

	mlm.isRestore = true;
	mlm.update(2000, 50);
	EXPECT_TRUE(mlm.lambdaCurrentlyGood);
	EXPECT_FALSE(mlm.isCut());
}
