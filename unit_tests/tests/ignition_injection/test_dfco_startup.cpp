#include "pch.h"

using ::testing::_;

class DfcoStartup : public ::testing::Test {
protected:
	EngineTestHelper eth{engine_type_e::TEST_ENGINE};

	void SetUp() override {
		EXPECT_CALL(*eth.mockAirmass, getAirmass(_, _)).WillRepeatedly(Return(AirmassResult{1.0f, 50.0f}));
		// RPM is driven directly, so these fuel tests do not need trigger reconfiguration.
		engineConfiguration->isIgnitionEnabled = false;
		engineConfiguration->stoichRatioPrimary = 10;
		engineConfiguration->coastingFuelCutEnabled = true;
		engineConfiguration->dfcoDelay = 0;
		engineConfiguration->dfcoRetardDeg = 10;
		engineConfiguration->postCrankingFactor = 2;
		Sensor::setMockValue(SensorType::Clt, 90);
		Sensor::setMockValue(SensorType::Map, 20);
		Sensor::setMockValue(SensorType::DriverThrottleIntent, 0);
	}

	void setRpm(float rpm) {
		Sensor::setMockValue(SensorType::Rpm, rpm);
		engine->rpmCalculator.setRpmValue(rpm);
		engine->periodicFastCallback();
	}

	void advance(int microseconds) {
		eth.moveTimeForwardUs(microseconds);
		engine->periodicFastCallback();
	}

	void expectFuel(bool cut) {
		EXPECT_EQ(cut, engine->module<DfcoController>()->cutFuel());
		if (cut) {
			EXPECT_FLOAT_EQ(0, engine->cylinders[0].getInjectionMass());
		} else {
			EXPECT_GT(engine->cylinders[0].getInjectionMass(), 0);
		}
	}

	void start() {
		setRpm(400);
		ASSERT_TRUE(engine->rpmCalculator.isCranking());
		advance(100'000);
		setRpm(2000);
		ASSERT_TRUE(engine->rpmCalculator.isRunning());
	}
};

TEST_F(DfcoStartup, crankingAboveDfcoThreshold) {
	// A zero hold-off must not permit DFCO to cancel cranking fuel.
	engineConfiguration->dfcoStartupDelay = 0;
	engineConfiguration->cranking.rpm = 2500;
	setRpm(2000);
	ASSERT_TRUE(engine->rpmCalculator.isCranking());
	expectFuel(false);
	EXPECT_FLOAT_EQ(0, engine->module<DfcoController>()->getTimingRetard());
	advance(2'000'000);
	expectFuel(false);
	setRpm(3000);
	expectFuel(true);
}

TEST_F(DfcoStartup, hotRestartDuringScalarTaper) {
	ASSERT_EQ(10, engineConfiguration->dfcoStartupDelay);
	std::ostringstream trace;
	trace << "runtime,rpm,cut,mass,retard,postCrankingFactor\n";
	auto sample = [&]() {
		trace << engine->rpmCalculator.getSecondsSinceEngineStart(getTimeNowNt()) << ','
			  << Sensor::getOrZero(SensorType::Rpm) << ',' << engine->module<DfcoController>()->cutFuel() << ','
			  << engine->cylinders[0].getInjectionMass() << ',' << engine->module<DfcoController>()->getTimingRetard()
			  << ',' << engine->fuelComputer.running.postCrankingFuelCorrection << '\n';
	};
	start();
	sample();
	expectFuel(false);
	advance(2'000'000);
	sample();
	EXPECT_NEAR(1.8f, engine->fuelComputer.running.postCrankingFuelCorrection, 0.001f);
	expectFuel(false);
	advance(7'990'000);
	sample();
	expectFuel(false);
	// The gate uses current running time, not the previous callback's enrichment factor.
	advance(10'000);
	sample();
	expectFuel(true);
	EXPECT_FLOAT_EQ(1, engine->fuelComputer.running.postCrankingFuelCorrection);
	RecordProperty("restartTrace", trace.str());

	// A second start must receive the same protection.
	setRpm(0);
	advance(1'000'000);
	start();
	expectFuel(false);
	advance(10'000'000);
	expectFuel(true);
}

TEST_F(DfcoStartup, restartWithoutCrankingCallback) {
	start();
	advance(10'000'000);
	expectFuel(true);
	setRpm(0);
	advance(60'000'000);

	// The first RPM update invokes a fast callback while the state is still STOPPED.
	// No cranking callback occurs on this direct transition to RUNNING.
	Sensor::setMockValue(SensorType::Rpm, 2000);
	engine->rpmCalculator.setRpmValue(2000);
	expectFuel(false);
	ASSERT_TRUE(engine->rpmCalculator.isRunning());
	engine->periodicFastCallback();
	expectFuel(false);
	EXPECT_GT(engine->fuelComputer.running.timeSinceCrankingInSecs, 10);
	advance(9'990'000);
	expectFuel(false);
	advance(10'000);
	expectFuel(true);
}

TEST_F(DfcoStartup, zeroHoldoffAllowsImmediateRunningCut) {
	engineConfiguration->dfcoStartupDelay = 0;
	Sensor::setMockValue(SensorType::Rpm, 2000);
	engine->rpmCalculator.setRpmValue(2000);
	// The callback before the STOPPED -> RUNNING transition must still inhibit DFCO.
	expectFuel(false);
	engine->periodicFastCallback();
	expectFuel(true);
}

TEST_F(DfcoStartup, configurableHoldoff) {
	engineConfiguration->dfcoStartupDelay = 3;
	start();
	advance(2'990'000);
	expectFuel(false);
	advance(10'000);
	expectFuel(true);
}

TEST_F(DfcoStartup, spinningUpAboveDfcoThreshold) {
	engineConfiguration->dfcoStartupDelay = 0;
	engine->rpmCalculator.setSpinningUp(getTimeNowNt());
	Sensor::setMockValue(SensorType::Rpm, 2000);
	engine->rpmCalculator.assignRpmValue(2000);
	ASSERT_TRUE(engine->rpmCalculator.isSpinningUp());
	ASSERT_TRUE(engine->rpmCalculator.isCranking());
	engine->periodicFastCallback();
	expectFuel(false);
	setRpm(2000);
	expectFuel(true);
}

TEST_F(DfcoStartup, cutDelayStartsAfterStartupGate) {
	engineConfiguration->dfcoDelay = 1;
	start();
	advance(9'999'000);
	expectFuel(false);
	EXPECT_FLOAT_EQ(0, engine->module<DfcoController>()->getTimingRetard());
	advance(1000);
	expectFuel(false);
	// Timing retard follows DFCO eligibility during the existing fuel cut delay.
	EXPECT_FLOAT_EQ(10, engine->module<DfcoController>()->getTimingRetard());
	advance(990'000);
	expectFuel(false);
	advance(10'000);
	expectFuel(true);
}

TEST_F(DfcoStartup, rpmHysteresisAfterStartupGate) {
	start();
	setRpm(1400);
	advance(10'000'000);
	expectFuel(false);
	setRpm(1600);
	expectFuel(true);
	setRpm(1400);
	expectFuel(true);
	setRpm(1200);
	expectFuel(false);
}

// Table enrichment deliberately stays above unity beyond its last runtime bin.
// Neither mode nor enrichment duration/factor may extend the explicit DFCO gate.
class DfcoStartupEnrichment : public DfcoStartup, public ::testing::WithParamInterface<std::tuple<bool, int, float>> {};

TEST_P(DfcoStartupEnrichment, holdoffIndependentOfEnrichment) {
	const auto [table, duration, factor] = GetParam();
	engineConfiguration->postCrankingFuelUseTable = table;
	engineConfiguration->postCrankingDurationSec = duration;
	engineConfiguration->postCrankingFactor = factor;
	setTable(config->postCrankingEnrichTable, factor);
	start();
	advance(9'990'000);
	expectFuel(false);
	advance(10'000);
	expectFuel(true);
	if (table) {
		EXPECT_FLOAT_EQ(factor, engine->fuelComputer.running.postCrankingFuelCorrection);
	}
}

INSTANTIATE_TEST_SUITE_P(
		Tapers,
		DfcoStartupEnrichment,
		::testing::Values(
				std::make_tuple(false, 10, 1.0f),
				std::make_tuple(false, 0, 2.0f),
				std::make_tuple(false, 30, 2.0f),
				std::make_tuple(true, 0, 1.0f),
				std::make_tuple(true, 30, 2.0f)));
