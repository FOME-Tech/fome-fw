#include "pch.h"

#include "boost_control.h"

using ::testing::_;
using ::testing::StrictMock;

TEST(BoostControl, Setpoint) {
	MockVp3d targetMap;

	// Just pass TPS input to output
	EXPECT_CALL(targetMap, getValue(_, _)).WillRepeatedly([](float xRpm, float tps) { return tps; });

	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->boostType = CLOSED_LOOP;

	BoostController bc;

	// Should return unexpected without a pedal map cfg'd
	EXPECT_EQ(bc.getSetpoint(), unexpected);

	// Now init with mock target map
	bc.init(nullptr, nullptr, &targetMap, nullptr);

	// Should still return unxepected since TPS is invalid
	EXPECT_EQ(bc.getSetpoint(), unexpected);

	// Configure TPS, should get passthru of tps value
	Sensor::setMockValue(SensorType::DriverThrottleIntent, 35.0f);
	EXPECT_FLOAT_EQ(bc.getSetpoint().value_or(-1), 35.0f);

	// Back in open loop mode, setpoint should be 0
	engineConfiguration->boostType = OPEN_LOOP;
	EXPECT_FLOAT_EQ(bc.getSetpoint().value_or(-1), 0);
}

TEST(BoostControl, ObservePlant) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	BoostController bc;

	Sensor::resetMockValue(SensorType::Map);
	// Check that invalid MAP returns unexpected
	EXPECT_EQ(bc.observePlant(), unexpected);

	// Test valid MAP value
	Sensor::setMockValue(SensorType::Map, 150);

	EXPECT_FLOAT_EQ(bc.observePlant().value_or(0), 150.0f);
}

TEST(BoostControl, OpenLoop) {
	MockVp3d openMap;

	// Just pass MAP input to output
	EXPECT_CALL(openMap, getValue(_, _)).WillRepeatedly([](float xRpm, float tps) { return tps; });

	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	BoostController bc;

	// Without table set, should return unexpected
	EXPECT_EQ(bc.getOpenLoop(0), unexpected);

	bc.init(nullptr, &openMap, nullptr, nullptr);

	// Should pass TPS value thru
	Sensor::setMockValue(SensorType::DriverThrottleIntent, 47.0f);
	EXPECT_FLOAT_EQ(bc.getOpenLoop(0).value_or(-1), 47.0f);
}

TEST(BoostControl, TestClosedLoop) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	BoostController bc;

	pid_s pidCfg = {
			1,
			0,
			0, // P controller, easier to test
			0, // no offset
			-100,
			100, // min/max output
			0	 // alignment pad
	};

	bc.init(nullptr, nullptr, nullptr, &pidCfg);

	// Enable closed loop
	engineConfiguration->boostType = CLOSED_LOOP;
	// Minimum 75kpa
	engineConfiguration->minimumBoostClosedLoopMap = 75;

	// At 0 RPM, closed loop is disabled
	Sensor::setMockValue(SensorType::Rpm, 0);
	EXPECT_EQ(0, bc.getClosedLoop(150, 100).value_or(-1000));

	// Stopped engine, disable closed loop
	Sensor::setMockValue(SensorType::Rpm, 0);
	EXPECT_EQ(0, bc.getClosedLoop(150, 50).value_or(-1000));

	// With RPM, we should get an output
	Sensor::setMockValue(SensorType::Rpm, 1000);
	// Actual is below target -> positive output
	EXPECT_FLOAT_EQ(50, bc.getClosedLoop(150, 100).value_or(-1000));
	// MAP below target -> returns 0, closed loop disabled
	EXPECT_FLOAT_EQ(0, bc.getClosedLoop(150, 50).value_or(-1000));
	// Actual is above target -> negative output
	EXPECT_FLOAT_EQ(-25.0f, bc.getClosedLoop(150, 175).value_or(-1000));

	// Disabling closed loop should return 0
	engineConfiguration->boostType = OPEN_LOOP;
	EXPECT_FLOAT_EQ(0, bc.getClosedLoop(150, 175).value_or(-1000));
}

TEST(BoostControl, SetOutput) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	engineConfiguration->isBoostControlEnabled = true;

	StrictMock<MockPwm> pwm;
	StrictMock<MockEtb> etb;
	BoostController bc;

	// ETB wastegate position & PWM should both be set
	EXPECT_CALL(etb, setWastegatePosition(25.0f));
	EXPECT_CALL(pwm, setSimplePwmDutyCycle(0.25f));

	// Don't crash if not init'd (don't deref null ptr m_pwm)
	EXPECT_NO_THROW(bc.setOutput(25.0f));

	// Init with mock PWM device and ETB
	bc.init(&pwm, nullptr, nullptr, nullptr);
	engine->etbControllers[0] = &etb;

	bc.setOutput(25.0f);
}

TEST(BoostControl, IndependentCorrectionAxesPreserveDriverIntentAndMissingSourcesUseSafeDuty) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->isBoostControlEnabled = true;
	engineConfiguration->boostType = CLOSED_LOOP;
	engineConfiguration->boostControlSafeDutyCycle = 17;
	Sensor::setMockValue(SensorType::Rpm, 2000);
	Sensor::setMockValue(SensorType::DriverThrottleIntent, 35);
	Sensor::setMockValue(SensorType::Tps1, 20);
	Sensor::setMockValue(SensorType::Map, 80);
	Sensor::setMockValue(SensorType::Clt, 70);
	StrictMock<MockVp3d> parentTable;
	EXPECT_CALL(parentTable, getValue(2000, 35)).WillRepeatedly(Return(40));
	StrictMock<MockPwm> pwm;
	BoostController bc;
	bc.init(&pwm, &parentTable, &parentTable, nullptr);
	config->boostOpenLoopBlendXAxis[0] = GPPWM_Tps;
	config->boostClosedLoopBlendXAxis[0] = GPPWM_Tps;
	for (auto* blend : {&config->boostOpenLoopBlends[0], &config->boostClosedLoopBlends[0]}) {
		blend->blendParameter = GPPWM_Clt;
		blend->yAxisOverride = GPPWM_Tps;
		setLinearCurve(blend->rpmBins, 0, 100, 1);
		setLinearCurve(blend->loadBins, 0, 100, 1);
		setLinearCurve(blend->blendBins, 0, 100, 1);
		setArrayValues(blend->blendValues, 100);
		for (size_t row = 0; row < efi::size(blend->table); row++) {
			for (size_t column = 0; column < efi::size(blend->table[row]); column++) {
				blend->table[row][column] = (blend->loadBins[row] + blend->rpmBins[column]) * 0.1f;
			}
		}
	}
	EXPECT_NEAR(bc.getOpenLoop(0).value_or(0), 45.5f, 0.2f);
	EXPECT_NEAR(bc.getSetpoint().value_or(0), 45.5f, 0.2f);
	EXPECT_FLOAT_EQ(engine->outputChannels.boostOpenLoopBlendXAxisValue[0], 35);
	EXPECT_FLOAT_EQ(bc.boostOpenLoopBlendYAxis[0], 20);

	// The parent coordinates still work, but the selected correction Y fails.
	config->boostOpenLoopBlends[0].yAxisOverride = GPPWM_EffectiveMap;
	config->boostClosedLoopBlends[0].yAxisOverride = GPPWM_EffectiveMap;
	const auto failedDuty = bc.getOpenLoop(0);
	EXPECT_FALSE(failedDuty.Valid);
	EXPECT_FALSE(bc.getSetpoint().Valid);
	EXPECT_CALL(pwm, setSimplePwmDutyCycle(0.17f));
	bc.setOutput(failedDuty);

	config->boostOpenLoopBlends[0].yAxisOverride = GPPWM_Tps;
	config->boostClosedLoopBlends[0].yAxisOverride = GPPWM_Tps;
	config->boostOpenLoopBlendXAxis[0] = GPPWM_EffectiveMap;
	config->boostClosedLoopBlendXAxis[0] = GPPWM_EffectiveMap;
	EXPECT_FALSE(bc.getOpenLoop(0).Valid);
	EXPECT_FALSE(bc.getSetpoint().Valid);

	config->boostOpenLoopBlendXAxis[0] = GPPWM_Tps;
	config->boostClosedLoopBlendXAxis[0] = GPPWM_Tps;
	Sensor::setInvalidMockValue(SensorType::Clt);
	EXPECT_FALSE(bc.getOpenLoop(0).Valid);
	EXPECT_FALSE(bc.getSetpoint().Valid);

	// Disabled corrections require none of their configured inputs.
	config->boostOpenLoopBlends[0].blendParameter = GPPWM_Zero;
	config->boostClosedLoopBlends[0].blendParameter = GPPWM_Zero;
	config->boostOpenLoopBlendXAxis[0] = GPPWM_EffectiveMap;
	config->boostClosedLoopBlendXAxis[0] = GPPWM_EffectiveMap;
	config->boostOpenLoopBlends[0].yAxisOverride = GPPWM_EffectiveMap;
	config->boostClosedLoopBlends[0].yAxisOverride = GPPWM_EffectiveMap;
	EXPECT_FLOAT_EQ(bc.getOpenLoop(0).value_or(0), 40);
	EXPECT_FLOAT_EQ(bc.getSetpoint().value_or(0), 40);
}
