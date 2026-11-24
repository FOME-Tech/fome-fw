#include "pch.h"

static void setupFixedBaro() {
	engineConfiguration->useFixedBaroCorrFromMap = true;
	// No trigger events since boot, so the engine hasn't moved recently
	advanceTimeUs(10e6);
}

TEST(FixedBaroFromMap, WaitsForFirstValidMap) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setupFixedBaro();

	// No MAP reading yet: nothing is captured, baro stays absent
	engine->periodicSlowCallback();
	EXPECT_FALSE(Sensor::hasSensor(SensorType::BarometricPressure));

	// First valid reading gets captured
	Sensor::setMockValue(SensorType::MapSlow, 85);
	engine->periodicSlowCallback();
	EXPECT_FLOAT_EQ(85, Sensor::getOrZero(SensorType::BarometricPressure));

	// Later MAP readings don't change it
	Sensor::setMockValue(SensorType::MapSlow, 40);
	engine->periodicSlowCallback();
	EXPECT_FLOAT_EQ(85, Sensor::getOrZero(SensorType::BarometricPressure));
}

TEST(FixedBaroFromMap, DisabledDoesNothing) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setupFixedBaro();
	engineConfiguration->useFixedBaroCorrFromMap = false;

	Sensor::setMockValue(SensorType::MapSlow, 85);
	engine->periodicSlowCallback();
	EXPECT_FALSE(Sensor::hasSensor(SensorType::BarometricPressure));
}

static void checkRejected(float mapKpa) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setupFixedBaro();

	Sensor::setMockValue(SensorType::MapSlow, mapKpa);
	engine->periodicSlowCallback();
	EXPECT_FALSE(Sensor::hasSensor(SensorType::BarometricPressure));

	// One shot: a later in-range reading isn't used either
	Sensor::setMockValue(SensorType::MapSlow, 95);
	engine->periodicSlowCallback();
	EXPECT_FALSE(Sensor::hasSensor(SensorType::BarometricPressure));
}

TEST(FixedBaroFromMap, RejectsOutOfRange) {
	checkRejected(59);
	checkRejected(111);
}

TEST(FixedBaroFromMap, AcceptsRangeLimits) {
	{
		EngineTestHelper eth(engine_type_e::TEST_ENGINE);
		setupFixedBaro();
		Sensor::setMockValue(SensorType::MapSlow, 60);
		engine->periodicSlowCallback();
		EXPECT_FLOAT_EQ(60, Sensor::getOrZero(SensorType::BarometricPressure));
	}
	{
		EngineTestHelper eth(engine_type_e::TEST_ENGINE);
		setupFixedBaro();
		Sensor::setMockValue(SensorType::MapSlow, 110);
		engine->periodicSlowCallback();
		EXPECT_FLOAT_EQ(110, Sensor::getOrZero(SensorType::BarometricPressure));
	}
}

TEST(FixedBaroFromMap, NoCaptureWhileRunning) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setupFixedBaro();

	// Engine is spinning when the first valid MAP sample arrives (eg ECU reset while running)
	eth.fireTriggerEvents2(/*count*/ 36, /*delayMs*/ 5);
	ASSERT_TRUE(engine->triggerCentral.engineMovedRecently(getTimeNowNt()));

	Sensor::setMockValue(SensorType::MapSlow, 70);
	engine->periodicSlowCallback();
	EXPECT_FALSE(Sensor::hasSensor(SensorType::BarometricPressure));

	// Not retried once the engine stops: the power-up opportunity is gone
	advanceTimeUs(10e6);
	engine->periodicSlowCallback();
	EXPECT_FALSE(Sensor::hasSensor(SensorType::BarometricPressure));
}

TEST(FixedBaroFromMap, NoCaptureWithRpm) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setupFixedBaro();

	Sensor::setMockValue(SensorType::Rpm, 250);
	Sensor::setMockValue(SensorType::MapSlow, 70);
	engine->updateSlowSensors();
	EXPECT_FALSE(Sensor::hasSensor(SensorType::BarometricPressure));
}
