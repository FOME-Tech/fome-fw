/**
 * @file test_first_motion_injection.cpp
 *
 * Cranking fuel started on first motion: the first cranking cycle's fuel goes in on every injector at
 * once a few teeth after the engine starts turning, and the cranking injections after trigger sync
 * carry on from the second cranking cycle.
 */

#include "pch.h"

#include "harness/engine_invariants.h"
#include "speed_density_base.h"

#include <map>

using ::testing::_;

namespace {

constexpr float crankRpm = 200;

// Cranking duration multiplier for cranking cycle 1, 2, 3...
float cycleMultiplier(int cycle) {
	return 4.5f - 0.5f * cycle;
}

// A four cylinder engine with a crank-mounted 36-2 wheel and no cam, cranking in batch mode
struct FirstMotionRig {
	explicit FirstMotionRig(bool enabled) {
		setCrankOperationMode();
		setupSimpleTestEngineWithMaf(&eth, IM_BATCH, trigger_type_e::TT_TOOTHED_WHEEL_36_2);
		engine->tdcMarkEnabled = false;

		engineConfiguration->crankingInjectionMode = IM_BATCH;
		// Stay cranking the whole test
		engineConfiguration->cranking.rpm = 999;
		engineConfiguration->crankingFuelOnFirstMotion = enabled;

		// Fixed cranking fuel: 10mg, x2 for coolant temperature, x the cycle multiplier
		engineConfiguration->useRunningMathForCranking = false;
		engineConfiguration->cranking.baseFuel = 10;
		engineConfiguration->flexCranking = false;
		setArrayValues(config->crankingFuelCoef, 2.0f);
		setArrayValues(config->crankingTpsCoef, 1.0f);
		for (size_t i = 0; i < efi::size(config->crankingCycleBins); i++) {
			config->crankingCycleBins[i] = i + 1;
			config->crankingCycleCoef[i] = cycleMultiplier(i + 1);
		}

		engineConfiguration->injector.flow = 200;
		EXPECT_CALL(*eth.mockAirmass, getAirmass(_, _)).WillRepeatedly(Return(AirmassResult{0.1f, 50.0f}));

		// Start part way round the wheel, so there are plenty of teeth before the first sync point
		sim = std::make_unique<EngineSim>(eth, &recorder, 90);
	}

	// Times at which all four injectors opened together
	std::vector<int64_t> allInjectorsOpenTimes() const {
		std::map<int64_t, int> opensAt;
		for (const auto& pulse : recorder.pulses(OutputKind::Injector)) {
			opensAt[pulse.startUs]++;
		}

		std::vector<int64_t> result;
		for (const auto& [time, count] : opensAt) {
			if (count == 4) {
				result.push_back(time);
			}
		}

		return result;
	}

	// Cranking multiplier in use right now, and the cranking cycle the engine is on
	float durationCoefficient() const {
		return engine->engineState.crankingFuel.durationCoefficient;
	}
	uint32_t crankingCycle() const {
		return engine->rpmCalculator.getRevolutionCounterSinceStart();
	}

	EngineTestHelper eth{engine_type_e::TEST_ENGINE};
	OutputRecorder recorder;
	std::unique_ptr<EngineSim> sim;
};

// Fuel mass of the first cranking cycle in the rig: 10mg x 2 x cycle 1's multiplier
const float firstCycleMass = 0.010f * 2 * cycleMultiplier(1);

} // namespace

TEST(FirstMotionInjection, FiresOnAllInjectorsWhenEngineStartsToTurn) {
	FirstMotionRig rig(true);
	auto startAngle = rig.sim->angle();

	rig.sim->setRpm(crankRpm);
	rig.sim->runDegrees(90);

	// No sync yet, so nothing else could have injected
	ASSERT_EQ(0, Sensor::getOrZero(SensorType::Rpm));

	auto pulses = rig.recorder.pulses(OutputKind::Injector);
	ASSERT_EQ(4u, pulses.size());

	float expectedMs = engine->module<InjectorModelPrimary>()->getInjectionDuration(firstCycleMass);
	for (const auto& pulse : pulses) {
		// All together, on the third tooth (teeth are 10 degrees apart)
		EXPECT_EQ(pulses[0].startUs, pulse.startUs);
		EXPECT_NEAR(startAngle + 30, pulse.startAngle, 10);

		EXPECT_FALSE(pulse.stillActive);
		EXPECT_NEAR(expectedMs, pulse.durationMs(), 0.01f);
	}

	EXPECT_TRUE(engine->outputChannels.crankingFirstMotionPulse);
}

TEST(FirstMotionInjection, DisabledWaitsForSync) {
	FirstMotionRig rig(false);

	rig.sim->setRpm(crankRpm);
	rig.sim->runDegrees(60);
	EXPECT_EQ(0u, rig.recorder.pulses(OutputKind::Injector).size());
	EXPECT_FALSE(engine->outputChannels.crankingFirstMotionPulse);

	// Cranking injection still starts once synchronized
	rig.sim->runRevolutions(3);
	EXPECT_GT(rig.recorder.pulses(OutputKind::Injector).size(), 0u);
	EXPECT_EQ(0u, rig.allInjectorsOpenTimes().size());
}

TEST(FirstMotionInjection, OncePerStart) {
	FirstMotionRig rig(true);

	rig.sim->setRpm(crankRpm);
	rig.sim->runRevolutions(8);

	EXPECT_EQ(1u, rig.allInjectorsOpenTimes().size());
}

TEST(FirstMotionInjection, NothingWithoutRotation) {
	FirstMotionRig rig(true);

	// Key on, engine not turning
	rig.sim->runSeconds(2);

	EXPECT_EQ(0u, rig.recorder.pulses(OutputKind::Injector).size());
}

TEST(FirstMotionInjection, FiresAgainAfterEngineStandsStill) {
	FirstMotionRig rig(true);

	rig.sim->setRpm(crankRpm);
	rig.sim->runRevolutions(4);
	ASSERT_EQ(1u, rig.allInjectorsOpenTimes().size());

	// Stops, sits for a moment, then cranks again
	rig.sim->setRpm(0);
	rig.sim->runSeconds(2);
	EXPECT_FALSE(engine->outputChannels.crankingFirstMotionPulse);

	rig.sim->setRpm(crankRpm);
	rig.sim->runDegrees(60);
	EXPECT_EQ(2u, rig.allInjectorsOpenTimes().size());
}

TEST(FirstMotionInjection, NoPulseDuringFloodClear) {
	FirstMotionRig rig(true);
	engineConfiguration->isCylinderCleanupEnabled = true;
	Sensor::setMockValue(SensorType::DriverThrottleIntent, 100);

	rig.sim->setRpm(crankRpm);
	rig.sim->runDegrees(60);

	EXPECT_EQ(0u, rig.recorder.pulses(OutputKind::Injector).size());
	EXPECT_FALSE(engine->outputChannels.crankingFirstMotionPulse);

	// Pedal released: cranking carries on as if this feature was off, from the first cycle
	Sensor::setMockValue(SensorType::DriverThrottleIntent, 0);
	rig.sim->runRevolutions(3);
	EXPECT_EQ(0u, rig.allInjectorsOpenTimes().size());
	EXPECT_FLOAT_EQ(cycleMultiplier(rig.crankingCycle()), rig.durationCoefficient());
}

// After the first motion pulse, cranking injections use the next cycle's multiplier, but the cranking
// cycle counter itself (which other things use) isn't shifted
TEST(FirstMotionInjection, LaterCyclesUseNextMultiplier) {
	std::vector<uint32_t> cyclesWithout;
	std::vector<uint32_t> cyclesWith;

	{
		FirstMotionRig rig(false);
		rig.sim->setRpm(crankRpm);

		for (int i = 0; i < 4; i++) {
			rig.sim->runRevolutions(2);
			ASSERT_GE(rig.crankingCycle(), 1u);
			EXPECT_FLOAT_EQ(cycleMultiplier(rig.crankingCycle()), rig.durationCoefficient()) << i;
			cyclesWithout.push_back(rig.crankingCycle());
		}
	}

	{
		FirstMotionRig rig(true);
		rig.sim->setRpm(crankRpm);

		for (int i = 0; i < 4; i++) {
			rig.sim->runRevolutions(2);
			EXPECT_FLOAT_EQ(cycleMultiplier(rig.crankingCycle() + 1), rig.durationCoefficient()) << i;
			cyclesWith.push_back(rig.crankingCycle());
		}
	}

	EXPECT_EQ(cyclesWithout, cyclesWith);
}

TEST(FirstMotionInjection, FuelMassFollowsCrankingFuel) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->crankingFuelOnFirstMotion = true;
	engineConfiguration->cranking.baseFuel = 10;
	engineConfiguration->flexCranking = false;
	setArrayValues(config->crankingFuelCoef, 2.0f);
	setArrayValues(config->crankingTpsCoef, 1.0f);
	for (size_t i = 0; i < efi::size(config->crankingCycleBins); i++) {
		config->crankingCycleBins[i] = i + 1;
		config->crankingCycleCoef[i] = cycleMultiplier(i + 1);
	}

	auto dut = engine->module<FirstMotionInjection>();

	// Fixed cranking fuel
	engineConfiguration->useRunningMathForCranking = false;
	dut->onFastCallback();
	EXPECT_FLOAT_EQ(firstCycleMass, dut->getFuelMass());

	// Pedal multiplier applies too
	Sensor::setMockValue(SensorType::DriverThrottleIntent, 50);
	setArrayValues(config->crankingTpsCoef, 0.5f);
	dut->onFastCallback();
	EXPECT_FLOAT_EQ(firstCycleMass * 0.5f, dut->getFuelMass());
	setArrayValues(config->crankingTpsCoef, 1.0f);

	// Running math for cranking: there's no airmass with the engine stopped, so it uses a cylinder
	// filled at atmospheric pressure
	engineConfiguration->useRunningMathForCranking = true;
	engineConfiguration->displacement = 2.0f;
	setCylinderCount(4);
	Sensor::setMockValue(SensorType::BarometricPressure, 100);
	Sensor::setMockValue(SensorType::Iat, 20);

	mass_t airmass = idealGasLaw(0.5f, 100, 273.15f + 20);
	float baseFuel = engine->fuelComputer.getCycleFuel(airmass, 0, 100) * engineConfiguration->globalFuelCorrection;

	dut->onFastCallback();
	EXPECT_FLOAT_EQ(baseFuel * 2 * cycleMultiplier(1), dut->getFuelMass());
}
