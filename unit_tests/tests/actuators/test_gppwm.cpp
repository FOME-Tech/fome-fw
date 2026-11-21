#include "pch.h"

#include "gppwm_channel.h"
#include "gppwm.h"
#include "boost_control.h"

#include "mocks.h"

using ::testing::InSequence;
using ::testing::StrictMock;

TEST(GpPwm, OutputWithPwm) {
	GppwmChannel ch;

	gppwm_channel cfg;

	StrictMock<MockPwm> pwm;

	// Shouldn't throw with no config
	EXPECT_NO_THROW(ch.setOutput(10));

	{
		InSequence i;
		EXPECT_CALL(pwm, setSimplePwmDutyCycle(0.25f));
		EXPECT_CALL(pwm, setSimplePwmDutyCycle(0.75f));
		EXPECT_CALL(pwm, setSimplePwmDutyCycle(0.0f));
		EXPECT_CALL(pwm, setSimplePwmDutyCycle(1.0f));
	}

	ch.init(true, &pwm, nullptr, nullptr, &cfg);

	// Set the output - should set directly to PWM
	ch.setOutput(25.0f);
	ch.setOutput(75.0f);

	// Test clamping behavior - should clamp to [0, 100]
	ch.setOutput(-10.0f);
	ch.setOutput(110.0f);
}

TEST(GpPwm, OutputOnOff) {
	GppwmChannel ch;

	gppwm_channel cfg;
	cfg.onAboveDuty = 50;
	cfg.offBelowDuty = 40;

	MockOutputPin pin;

	{
		InSequence i;

		// Rising edge test
		EXPECT_CALL(pin, setValue(0));
		EXPECT_CALL(pin, setValue(1));
		EXPECT_CALL(pin, setValue(1));

		// Falling edge test
		EXPECT_CALL(pin, setValue(1));
		EXPECT_CALL(pin, setValue(0));
		EXPECT_CALL(pin, setValue(0));
	}

	ch.init(false, nullptr, &pin, nullptr, &cfg);

	// Test rising edge - these should output 0, 1, 1
	ch.setOutput(49.0f);
	ch.setOutput(51.0f);
	ch.setOutput(49.0f);

	// Test falling edge - these should output 1, 0, 0
	ch.setOutput(41.0f);
	ch.setOutput(39.0f);
	ch.setOutput(41.0f);
}

TEST(GpPwm, TestGetOutput) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	GppwmChannel ch;

	gppwm_channel cfg;
	cfg.loadAxis = GPPWM_Tps;
	cfg.rpmAxis = GPPWM_Rpm;
	cfg.dutyIfError = 21.0f;

	MockVp3d table;

	EXPECT_CALL(table, getValue(1200, 35.0f)).WillRepeatedly([](float x, float tps) { return tps; });

	ch.init(false, nullptr, nullptr, &table, &cfg);

	Sensor::resetAllMocks();

	// Should return dutyIfError
	EXPECT_FLOAT_EQ(21.0f, ch.getOutput().Result);

	// Set TPS, should return tps value
	Sensor::setMockValue(SensorType::Tps1, 35.0f);
	Sensor::setMockValue(SensorType::Rpm, 1200);
	EXPECT_FLOAT_EQ(35.0f, ch.getOutput().Result);
}

TEST(GpPwm, ReadSensorChannels) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	Sensor::setMockValue(SensorType::ClutchPosition, 12);
	Sensor::setMockValue(SensorType::FuelTemperature, 34);
	Sensor::setMockValue(SensorType::FuelLevel, 56);
	Sensor::setMockValue(SensorType::EcuInternalTemperature, 78);
	Sensor::setMockValue(SensorType::AuxSpeed1, 910);
	Sensor::setMockValue(SensorType::AuxSpeed2, 1112);
	Sensor::setMockValue(SensorType::AccelLat, -3.5f);
	Sensor::setMockValue(SensorType::AccelLon, 7.25f);
	Sensor::setMockValue(SensorType::FuelPressureLow, 380);
	Sensor::setMockValue(SensorType::FuelPressureHigh, 15000);

	EXPECT_FLOAT_EQ(12, readGppwmChannel(GPPWM_ClutchPosition).value_or(-1));
	EXPECT_FLOAT_EQ(34, readGppwmChannel(GPPWM_FuelTemp).value_or(-1));
	EXPECT_FLOAT_EQ(56, readGppwmChannel(GPPWM_FuelLevel).value_or(-1));
	EXPECT_FLOAT_EQ(78, readGppwmChannel(GPPWM_EcuTemp).value_or(-1));
	EXPECT_FLOAT_EQ(910, readGppwmChannel(GPPWM_AuxSpeed1).value_or(-1));
	EXPECT_FLOAT_EQ(1112, readGppwmChannel(GPPWM_AuxSpeed2).value_or(-1));
	EXPECT_FLOAT_EQ(-3.5f, readGppwmChannel(GPPWM_AccelLat).value_or(-1));
	EXPECT_FLOAT_EQ(7.25f, readGppwmChannel(GPPWM_AccelLon).value_or(-1));
	// Low side, not GDI rail pressure
	EXPECT_FLOAT_EQ(380, readGppwmChannel(GPPWM_FuelPressure).value_or(-1));

	// Missing sensors propagate as failures
	Sensor::resetMockValue(SensorType::ClutchPosition);
	EXPECT_EQ(readGppwmChannel(GPPWM_ClutchPosition), unexpected);
}

TEST(GpPwm, ReadTurboSpeedChannel) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	Sensor::resetMockValue(SensorType::TurbochargerSpeed);
	EXPECT_EQ(readGppwmChannel(GPPWM_TurboSpeed), unexpected);

	// Reported in thousands of RPM so it fits the axis bins
	Sensor::setMockValue(SensorType::TurbochargerSpeed, 185000);
	EXPECT_FLOAT_EQ(185, readGppwmChannel(GPPWM_TurboSpeed).value_or(-1));
}

TEST(GpPwm, ReadBoostChannels) {
	MockVp3d targetMap;
	// Pass TPS input through as the target
	EXPECT_CALL(targetMap, getValue(testing::_, testing::_)).WillRepeatedly([](float xRpm, float tps) { return tps; });

	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->isBoostControlEnabled = true;
	engineConfiguration->boostType = CLOSED_LOOP;

	auto& bc = engine->module<BoostController>().unmock();
	bc.init(nullptr, nullptr, &targetMap, nullptr);

	Sensor::setMockValue(SensorType::DriverThrottleIntent, 180);
	Sensor::setMockValue(SensorType::Map, 150);
	bc.getSetpoint();

	EXPECT_FLOAT_EQ(180, readGppwmChannel(GPPWM_BoostTarget).value_or(-1));
	// Below target is positive error
	EXPECT_FLOAT_EQ(30, readGppwmChannel(GPPWM_BoostError).value_or(-1));

	// Overboost is negative error
	Sensor::setMockValue(SensorType::Map, 200);
	EXPECT_FLOAT_EQ(-20, readGppwmChannel(GPPWM_BoostError).value_or(-1));

	// No MAP, no error
	Sensor::resetMockValue(SensorType::Map);
	EXPECT_EQ(readGppwmChannel(GPPWM_BoostError), unexpected);
	EXPECT_FLOAT_EQ(180, readGppwmChannel(GPPWM_BoostTarget).value_or(-1));

	// Open loop has no target
	engineConfiguration->boostType = OPEN_LOOP;
	EXPECT_EQ(readGppwmChannel(GPPWM_BoostTarget), unexpected);
	EXPECT_EQ(readGppwmChannel(GPPWM_BoostError), unexpected);

	// Nor does disabled boost control
	engineConfiguration->boostType = CLOSED_LOOP;
	engineConfiguration->isBoostControlEnabled = false;
	EXPECT_EQ(readGppwmChannel(GPPWM_BoostTarget), unexpected);
}
