#include "pch.h"

#include "throttle_model.h"

namespace {
// Configure a simple, fully-defined airpath: 2.0L 4-cylinder running at 3000 RPM,
// with a linear throttle effective-area table topping out at 400 g/s.
void setupAirpath(EngineTestHelper& eth) {
	engineConfiguration->displacement = 2.0f;
	engineConfiguration->cylindersCount = 4;
	engineConfiguration->twoStroke = false;

	// Linear effective-area table: 0% -> 0 g/s, 100% -> 400 g/s.
	for (size_t i = 0; i < efi::size(config->throttleEstimateEffectiveAreaBins); i++) {
		float tps = i * (100.0f / (efi::size(config->throttleEstimateEffectiveAreaBins) - 1));
		config->throttleEstimateEffectiveAreaBins[i] = tps;
		config->throttleEstimateEffectiveAreaValues[i] = tps * 4.0f; // 400 g/s at 100%
	}

	// The forward throttle model (used by the round-trip check) needs charge temperature.
	engine->engineState.sd.tChargeK = 300;

	// Neutralize the closed-loop airmass trim so the feed-forward path is exercised in
	// isolation. The trim's own behavior is covered by the dedicated trim tests below.
	engineConfiguration->torqueModel.airmassTrimKp = 0;
	engineConfiguration->torqueModel.airmassTrimKi = 0;
	engineConfiguration->torqueModel.airmassTrimAuthority = 25;
	engine->fuelComputer.sdAirMassInOneCylinder = 0;

	Sensor::setMockValue(SensorType::Rpm, 3000);
	Sensor::setMockValue(SensorType::Iat, 20);
	Sensor::setMockValue(SensorType::BarometricPressure, 100);
	Sensor::setMockValue(SensorType::Map, 50);
}

// Engine mass flow [g/s] for a given total per-cycle airmass, mirroring the production formula.
float flowForAirmass(float airmassPerCycle) {
	if (!engineConfiguration->twoStroke) {
		airmassPerCycle /= 2;
	}
	return airmassPerCycle * Sensor::getOrZero(SensorType::Rpm) / 60;
}
} // namespace

TEST(AirmassDispatcher, RoundTripsThroughThrottleModel) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setupAirpath(eth);

	auto& torqueModel = engine->module<TorqueModel>().unmock();
	auto& throttleModel = engine->module<ThrottleModel>().unmock();

	// 1.04 g/cycle -> 26 g/s, well into part-throttle at the mocked 50 kPa MAP.
	float targetAirmass = 1.04f;
	float expectedFlow = flowForAirmass(targetAirmass);

	torqueModel.airmassDispatcher.update(targetAirmass);
	percent_t throttle = torqueModel.getThrottleRequest();

	// The commanded throttle, run forward through the throttle model at the measured MAP,
	// should reproduce the requested airflow.
	float actualFlow = throttleModel.estimateThrottleFlow(/*tip*/ 100, throttle, /*map*/ 50, /*iat*/ 20);

	EXPECT_GT(throttle, 0);
	EXPECT_LT(throttle, 100);
	EXPECT_NEAR(actualFlow, expectedFlow, expectedFlow * 0.02f);
}

TEST(AirmassDispatcher, MonotonicInAirmass) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setupAirpath(eth);

	auto& torqueModel = engine->module<TorqueModel>().unmock();

	torqueModel.airmassDispatcher.update(0.6f);
	percent_t low = torqueModel.getThrottleRequest();

	torqueModel.airmassDispatcher.update(1.2f);
	percent_t high = torqueModel.getThrottleRequest();

	EXPECT_GT(high, low);
}

TEST(AirmassDispatcher, SaturatesToWideOpenWhenDemandExceedsCapacity) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setupAirpath(eth);

	auto& torqueModel = engine->module<TorqueModel>().unmock();

	// Far more air than a wide-open throttle could ever pass -> saturate to 100%.
	torqueModel.airmassDispatcher.update(50);

	EXPECT_FLOAT_EQ(torqueModel.getThrottleRequest(), 100);
}

// Regression: at key-on / cranking / closed-throttle decel the manifold sits near
// atmospheric (MAP ~= inlet pressure). A low torque demand there must NOT be mistaken
// for a boost-region "throttle can't restrict" condition and commanded wide open.
TEST(AirmassDispatcher, DoesNotCommandWideOpenAtLowDemandWithAtmosphericManifold) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setupAirpath(eth);

	auto& torqueModel = engine->module<TorqueModel>().unmock();

	// Manifold at inlet pressure, but only a trickle of air requested.
	Sensor::setMockValue(SensorType::Map, 100);
	torqueModel.airmassDispatcher.update(0.2f);

	percent_t throttle = torqueModel.getThrottleRequest();
	EXPECT_GT(throttle, 0);
	EXPECT_LT(throttle, 25);
}

TEST(AirmassDispatcher, ClosedWhenStoppedOrIdleDemand) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setupAirpath(eth);

	auto& torqueModel = engine->module<TorqueModel>().unmock();

	// No airflow requested.
	torqueModel.airmassDispatcher.update(0);
	EXPECT_FLOAT_EQ(torqueModel.getThrottleRequest(), 0);

	// Engine not turning.
	Sensor::setMockValue(SensorType::Rpm, 0);
	torqueModel.airmassDispatcher.update(0.30f);
	EXPECT_FLOAT_EQ(torqueModel.getThrottleRequest(), 0);
}

namespace {
void enableTrim(float kP) {
	engineConfiguration->torqueModel.airmassTrimKp = kP;
	engineConfiguration->torqueModel.airmassTrimKi = 0;
	engineConfiguration->torqueModel.airmassTrimAuthority = 25;
}
} // namespace

TEST(AirmassDispatcher, TrimOpensThrottleWhenStarvedOfAir) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setupAirpath(eth);
	enableTrim(20);

	auto& torqueModel = engine->module<TorqueModel>().unmock();

	float target = 1.04f;

	// Baseline: feedback exactly matches target -> no trim, pure feed-forward.
	engine->fuelComputer.sdAirMassInOneCylinder = target / engineConfiguration->cylindersCount;
	torqueModel.airmassDispatcher.update(target);
	percent_t baseline = torqueModel.getThrottleRequest();
	EXPECT_NEAR(torqueModel.airmassDispatcher.getAirmassTrim(), 0, 1e-3);

	// Measured airmass below target -> positive trim -> throttle opens further.
	engine->fuelComputer.sdAirMassInOneCylinder = 0.5f * target / engineConfiguration->cylindersCount;
	torqueModel.airmassDispatcher.update(target);

	EXPECT_GT(torqueModel.airmassDispatcher.getAirmassTrim(), 0);
	EXPECT_GT(torqueModel.getThrottleRequest(), baseline);
}

TEST(AirmassDispatcher, TrimClosesThrottleWhenOverAir) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setupAirpath(eth);
	enableTrim(20);

	auto& torqueModel = engine->module<TorqueModel>().unmock();

	float target = 1.04f;

	// Measured airmass above target -> negative trim -> less commanded flow.
	engine->fuelComputer.sdAirMassInOneCylinder = 1.5f * target / engineConfiguration->cylindersCount;
	torqueModel.airmassDispatcher.update(target);

	EXPECT_LT(torqueModel.airmassDispatcher.getAirmassTrim(), 0);
}

TEST(AirmassDispatcher, TrimResetsWhenClosed) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setupAirpath(eth);
	// Integral term so the trim can accumulate, then must be cleared on close.
	enableTrim(20);
	engineConfiguration->torqueModel.airmassTrimKi = 50;

	auto& torqueModel = engine->module<TorqueModel>().unmock();

	float target = 1.04f;
	engine->fuelComputer.sdAirMassInOneCylinder = 0.5f * target / engineConfiguration->cylindersCount;

	// Build up some trim over several cycles.
	for (int i = 0; i < 20; i++) {
		torqueModel.airmassDispatcher.update(target);
	}
	EXPECT_GT(torqueModel.airmassDispatcher.getAirmassTrim(), 0);

	// Demand drops to zero - the integrator must not carry windup into the next tip-in.
	torqueModel.airmassDispatcher.update(0);
	EXPECT_FLOAT_EQ(torqueModel.airmassDispatcher.getAirmassTrim(), 0);
}
