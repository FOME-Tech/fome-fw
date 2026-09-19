#include "pch.h"

static void updateFan1() {
	engine->module<FanControl1>()->onSlowCallback();
}

struct TestPwm : public IPwm {
	float lastDuty = 0;

	void setSimplePwmDutyCycle(float dutyCycle) override {
		lastDuty = dutyCycle;
	}
};

struct MockAc : public AcController {
	bool acState = false;

	bool isAcEnabled() const override {
		return acState;
	}
};

TEST(Actuators, Fan) {

	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	MockAc mockAc;
	engine->module<AcController>().set(&mockAc);

	// Turn on the ignition
	engine->module<FanControl1>()->onIgnitionStateChanged(true);

	engineConfiguration->fanOnTemperature = 90;
	engineConfiguration->fanOffTemperature = 80;
	engineConfiguration->enableFan1WithAc = false;

	// Cold, fan should be off
	Sensor::setMockValue(SensorType::Clt, 75);
	updateFan1();
	EXPECT_EQ(false, enginePins.fanRelay.getLogicValue());

	// Between thresholds, should still be off
	Sensor::setMockValue(SensorType::Clt, 85);
	updateFan1();
	EXPECT_EQ(false, enginePins.fanRelay.getLogicValue());

	// Hot, fan should turn on
	Sensor::setMockValue(SensorType::Clt, 95);
	updateFan1();
	EXPECT_EQ(true, enginePins.fanRelay.getLogicValue());

	// Between thresholds, should stay on
	Sensor::setMockValue(SensorType::Clt, 85);
	updateFan1();
	EXPECT_EQ(true, enginePins.fanRelay.getLogicValue());

	// Below threshold, should turn off
	Sensor::setMockValue(SensorType::Clt, 75);
	updateFan1();
	EXPECT_EQ(false, enginePins.fanRelay.getLogicValue());

	// Break the CLT sensor - fan turns on
	Sensor::setInvalidMockValue(SensorType::Clt);
	updateFan1();
	EXPECT_EQ(true, enginePins.fanRelay.getLogicValue());

	// CLT sensor back to normal, fan turns off
	Sensor::setMockValue(SensorType::Clt, 75);
	updateFan1();
	EXPECT_EQ(false, enginePins.fanRelay.getLogicValue());

	engineConfiguration->enableFan1WithAc = true;
	// Now AC is on, fan should turn on!
	mockAc.acState = true;
	updateFan1();
	EXPECT_EQ(true, enginePins.fanRelay.getLogicValue());

	// Turn off AC, fan should turn off too.
	mockAc.acState = false;
	updateFan1();
	EXPECT_EQ(false, enginePins.fanRelay.getLogicValue());

	// Back to hot, fan should turn on
	Sensor::setMockValue(SensorType::Clt, 95);
	updateFan1();
	EXPECT_EQ(true, enginePins.fanRelay.getLogicValue());

	// Engine starts cranking, fan should turn off
	engine->rpmCalculator.setRpmValue(100);
	updateFan1();
	EXPECT_EQ(false, enginePins.fanRelay.getLogicValue());

	// Engine running, fan should turn back on
	engine->rpmCalculator.setRpmValue(1000);
	updateFan1();
	EXPECT_EQ(true, enginePins.fanRelay.getLogicValue());

	// Stop the engine, fan should stay on
	engine->rpmCalculator.setRpmValue(0);
	updateFan1();
	EXPECT_EQ(true, enginePins.fanRelay.getLogicValue());

	// Set configuration to inhibit fan while engine is stopped, fan should stop
	engineConfiguration->disableFan1WhenStopped = true;
	updateFan1();
	EXPECT_EQ(false, enginePins.fanRelay.getLogicValue());
}

TEST(Actuators, FanPwm) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	MockAc mockAc;
	engine->module<AcController>().set(&mockAc);

	TestPwm testPwm;

	// Enable PWM mode for fan 1
	engineConfiguration->fan1UsePwmMode = true;
	engineConfiguration->fan1PwmXAxis = GPPWM_Zero; // Use Zero to avoid sensor mocking issues
	engineConfiguration->fanPwmSafetyDuty = 100;

	// Set up temperature thresholds
	engineConfiguration->fanOnTemperature = 90;
	engineConfiguration->fanOffTemperature = 80;
	engineConfiguration->enableFan1WithAc = false;

	// Set up simple duty tables - 50% when AC off, 75% when AC on
	setLinearCurve(config->fan1CltBins, 0, 100);
	setArrayValues(config->fan1XAxisBins, 0); // All zeros since X axis is Zero
	setTable(config->fan1DutyAcOff, 50);	  // 50% duty
	setTable(config->fan1DutyAcOn, 75);		  // 75% duty

	// Inject mock PWM
	engine->module<FanControl1>()->setMockPwm(&testPwm);

	// Turn on ignition and set engine running
	engine->module<FanControl1>()->onIgnitionStateChanged(true);
	engine->rpmCalculator.setRpmValue(1000);

	// Cold (below off threshold), fan should be off even in PWM mode
	Sensor::setMockValue(SensorType::Clt, 75);
	updateFan1();
	EXPECT_FLOAT_EQ(0, testPwm.lastDuty);
	EXPECT_EQ(false, engine->module<FanControl1>()->m_state);

	// Hot (above on threshold), fan should use PWM table
	Sensor::setMockValue(SensorType::Clt, 95);
	updateFan1();
	EXPECT_NEAR(0.50, testPwm.lastDuty, 0.01); // 50% duty from AC off table
	EXPECT_EQ(true, engine->module<FanControl1>()->m_state);

	// Turn on AC, should use AC on table (75%)
	mockAc.acState = true;
	engineConfiguration->enableFan1WithAc = true;
	updateFan1();
	EXPECT_NEAR(0.75, testPwm.lastDuty, 0.01); // 75% duty from AC on table
	EXPECT_EQ(true, engine->module<FanControl1>()->m_state);

	// Turn off AC, back to AC off table
	mockAc.acState = false;
	updateFan1();
	EXPECT_NEAR(0.50, testPwm.lastDuty, 0.01);

	// Cool down below off threshold, should go to 0
	Sensor::setMockValue(SensorType::Clt, 75);
	updateFan1();
	EXPECT_FLOAT_EQ(0, testPwm.lastDuty);
}

TEST(Actuators, FanPwmSafetyDuty) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	MockAc mockAc;
	engine->module<AcController>().set(&mockAc);

	TestPwm testPwm;

	// Enable PWM mode with Zero X axis (avoids sensor mocking complexity)
	engineConfiguration->fan1UsePwmMode = true;
	engineConfiguration->fan1PwmXAxis = GPPWM_Zero;
	engineConfiguration->fanPwmSafetyDuty = 80;

	// Set thresholds so fan is on
	engineConfiguration->fanOnTemperature = 90;
	engineConfiguration->fanOffTemperature = 80;
	engineConfiguration->enableFan1WithAc = false;

	// Set up duty tables - 50% duty everywhere
	setLinearCurve(config->fan1CltBins, 0, 100);
	setArrayValues(config->fan1XAxisBins, 0);
	setTable(config->fan1DutyAcOff, 50);
	setTable(config->fan1DutyAcOn, 50);

	engine->module<FanControl1>()->setMockPwm(&testPwm);
	engine->module<FanControl1>()->onIgnitionStateChanged(true);
	engine->rpmCalculator.setRpmValue(1000);

	// Cold first (like FanPwm test), fan should be off
	Sensor::setMockValue(SensorType::Clt, 75);
	updateFan1();
	EXPECT_FLOAT_EQ(0, testPwm.lastDuty);

	// Hot CLT, X axis is Zero (always valid) - should use table
	Sensor::setMockValue(SensorType::Clt, 95);
	updateFan1();
	EXPECT_NEAR(0.50, testPwm.lastDuty, 0.01);

	// Break CLT sensor - on/off logic returns true (broken CLT = fan on)
	// and getPwmDuty returns safety duty since CLT is needed for table lookup
	Sensor::resetMockValue(SensorType::Clt);
	updateFan1();
	EXPECT_NEAR(0.80, testPwm.lastDuty, 0.01);

	// Restore CLT - back to table lookup
	Sensor::setMockValue(SensorType::Clt, 95);
	updateFan1();
	EXPECT_NEAR(0.50, testPwm.lastDuty, 0.01);
}

TEST(Actuators, FanDisableAtSpeed) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	MockAc mockAc;
	engine->module<AcController>().set(&mockAc);

	engine->module<FanControl1>()->onIgnitionStateChanged(true);
	engine->rpmCalculator.setRpmValue(1000);

	engineConfiguration->fanOnTemperature = 90;
	engineConfiguration->fanOffTemperature = 80;
	engineConfiguration->enableFan1WithAc = false;

	// Hot, so the fan wants to run
	Sensor::setMockValue(SensorType::Clt, 95);

	// Threshold of 0 disables the feature: fan runs at any speed
	engineConfiguration->disableFan1AtSpeed = 0;
	Sensor::setMockValue(SensorType::VehicleSpeed, 150);
	updateFan1();
	EXPECT_EQ(true, enginePins.fanRelay.getLogicValue());
	EXPECT_FALSE(engine->module<FanControl1>()->disabledBySpeed);

	// Above the threshold, fan is off even though hot
	engineConfiguration->disableFan1AtSpeed = 60;
	Sensor::setMockValue(SensorType::VehicleSpeed, 100);
	updateFan1();
	EXPECT_EQ(false, enginePins.fanRelay.getLogicValue());
	EXPECT_TRUE(engine->module<FanControl1>()->disabledBySpeed);

	// Below the threshold, fan runs again
	Sensor::setMockValue(SensorType::VehicleSpeed, 50);
	updateFan1();
	EXPECT_EQ(true, enginePins.fanRelay.getLogicValue());

	// Exactly at the threshold is not above it
	Sensor::setMockValue(SensorType::VehicleSpeed, 60);
	updateFan1();
	EXPECT_EQ(true, enginePins.fanRelay.getLogicValue());

	// A failed speed sensor must never turn the fan off
	Sensor::setInvalidMockValue(SensorType::VehicleSpeed);
	updateFan1();
	EXPECT_EQ(true, enginePins.fanRelay.getLogicValue());
	EXPECT_FALSE(engine->module<FanControl1>()->disabledBySpeed);

	// ...and neither must a missing one
	Sensor::resetMockValue(SensorType::VehicleSpeed);
	updateFan1();
	EXPECT_EQ(true, enginePins.fanRelay.getLogicValue());

	// Speed overrides the AC request
	engineConfiguration->enableFan1WithAc = true;
	mockAc.acState = true;
	Sensor::setMockValue(SensorType::Clt, 75);
	Sensor::setMockValue(SensorType::VehicleSpeed, 100);
	updateFan1();
	EXPECT_EQ(false, enginePins.fanRelay.getLogicValue());

	Sensor::setMockValue(SensorType::VehicleSpeed, 30);
	updateFan1();
	EXPECT_EQ(true, enginePins.fanRelay.getLogicValue());

	// But a broken CLT still forces the fan on at speed
	engineConfiguration->enableFan1WithAc = false;
	mockAc.acState = false;
	Sensor::setInvalidMockValue(SensorType::Clt);
	Sensor::setMockValue(SensorType::VehicleSpeed, 100);
	updateFan1();
	EXPECT_EQ(true, enginePins.fanRelay.getLogicValue());
}

TEST(Actuators, FanDisableAtSpeedPwm) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	MockAc mockAc;
	engine->module<AcController>().set(&mockAc);

	TestPwm testPwm;

	engineConfiguration->fan1UsePwmMode = true;
	engineConfiguration->fan1PwmXAxis = GPPWM_Zero;
	engineConfiguration->fanOnTemperature = 90;
	engineConfiguration->fanOffTemperature = 80;
	engineConfiguration->disableFan1AtSpeed = 60;

	setLinearCurve(config->fan1CltBins, 0, 100);
	setArrayValues(config->fan1XAxisBins, 0);
	setTable(config->fan1DutyAcOff, 50);
	setTable(config->fan1DutyAcOn, 75);

	engine->module<FanControl1>()->setMockPwm(&testPwm);
	engine->module<FanControl1>()->onIgnitionStateChanged(true);
	engine->rpmCalculator.setRpmValue(1000);

	// Hot and slow: table duty
	Sensor::setMockValue(SensorType::Clt, 95);
	Sensor::setMockValue(SensorType::VehicleSpeed, 30);
	updateFan1();
	EXPECT_NEAR(0.50, testPwm.lastDuty, 0.01);

	// Hot and fast: no duty
	Sensor::setMockValue(SensorType::VehicleSpeed, 100);
	updateFan1();
	EXPECT_NEAR(0, testPwm.lastDuty, 0.01);
}

TEST(Actuators, FanDisableAtSpeedPerFan) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	MockAc mockAc;
	engine->module<AcController>().set(&mockAc);

	engine->module<FanControl1>()->onIgnitionStateChanged(true);
	engine->module<FanControl2>()->onIgnitionStateChanged(true);
	engine->rpmCalculator.setRpmValue(1000);

	engineConfiguration->fanOnTemperature = 90;
	engineConfiguration->fanOffTemperature = 80;
	engineConfiguration->fan2OnTemperature = 90;
	engineConfiguration->fan2OffTemperature = 80;

	// Only fan 2 has a speed limit
	engineConfiguration->disableFan1AtSpeed = 0;
	engineConfiguration->disableFan2AtSpeed = 60;

	Sensor::setMockValue(SensorType::Clt, 95);
	Sensor::setMockValue(SensorType::VehicleSpeed, 100);
	updateFan1();
	engine->module<FanControl2>()->onSlowCallback();

	EXPECT_EQ(true, enginePins.fanRelay.getLogicValue());
	EXPECT_EQ(false, enginePins.fanRelay2.getLogicValue());
}
