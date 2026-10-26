#include "pch.h"

#include "electronic_throttle_impl.h"
#include "fuel_math.h"
#include "limp_manager.h"
#include "prime_injection.h"
#include "spark_logic.h"
#include "vvt.h"

using ::testing::_;
using ::testing::Gt;

// These tests preserve the observed behavior so each finding can be reviewed
// independently before changing production control logic.
TEST(EngineControlReview, FailedMapReleasesActiveBoostFuelCut) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->boostCutPressure = 100;
	LimpManager dut;

	Sensor::setMockValue(SensorType::Map, 105);
	dut.updateState(2000, getTimeNowNt());
	ASSERT_FALSE(dut.allowInjection());

	// A valid pressure in the hysteresis band retains the cut.
	Sensor::setMockValue(SensorType::Map, 95);
	dut.updateState(2000, getTimeNowNt());
	ASSERT_FALSE(dut.allowInjection());

	// Losing MAP data substitutes zero, crossing the release threshold.
	Sensor::resetMockValue(SensorType::Map);
	ASSERT_FALSE(Sensor::get(SensorType::Map));
	dut.updateState(2000, getTimeNowNt());
	EXPECT_TRUE(dut.allowInjection());

	Sensor::setMockValue(SensorType::Map, 105);
	dut.updateState(2000, getTimeNowNt());
	EXPECT_FALSE(dut.allowInjection());
}

TEST(EngineControlReview, SecondaryInjectorDutyDoesNotTripFuelCut) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->enableStagedInjection = true;
	engineConfiguration->injectionMode = IM_SEQUENTIAL;
	engineConfiguration->maxInjectorDutyInstant = 60;
	engineConfiguration->maxInjectorDutySustained = 100;
	engine->rpmCalculator.setRpmValue(3000);
	ASSERT_TRUE(engine->rpmCalculator.isRunning());

	// At 3000 RPM a four-stroke cycle is 40 ms. Stage 2 is at 87.5%.
	engine->engineState.injectionDuration = 2;
	engine->engineState.injectionDurationStage2 = 35;
	ASSERT_LT(getInjectorDutyCycle(3000), 20);
	ASSERT_GT(getInjectorDutyCycleStage2(3000), 60);
	LimpManager secondaryDuty;
	secondaryDuty.updateState(3000, getTimeNowNt());
	EXPECT_TRUE(secondaryDuty.allowInjection());

	// The same excessive duration on stage 1 does trip the configured cut.
	engine->engineState.injectionDuration = 35;
	engine->engineState.injectionDurationStage2 = 0;
	LimpManager primaryDuty;
	primaryDuty.updateState(3000, getTimeNowNt());
	EXPECT_FALSE(primaryDuty.allowInjection());
}

TEST(EngineControlReview, VvtActivationDelayDropsFractionalSeconds) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setTimeNowUs(1e6);
	engineConfiguration->vvtControlMinRpm = 0;
	engineConfiguration->vvtControlMinClt = 0;
	engineConfiguration->vvtOutputMin[0] = 0;
	engineConfiguration->vvtOutputMax[0] = 100;
	Sensor::setMockValue(SensorType::Rpm, 1000);
	Sensor::setMockValue(SensorType::Clt, 70);
	Sensor::setMockValue(SensorType::BatteryVoltage, 14);
	engine->rpmCalculator.setRpmValue(1000);
	ASSERT_TRUE(engine->rpmCalculator.isRunning());
	engine->triggerCentral.vvtPosition[0][0].angle = 10;
	engine->triggerCentral.vvtPosition[0][0].t.reset();

	MockVp3d targetMap;
	EXPECT_CALL(targetMap, getValue(_, _)).WillRepeatedly(::testing::Return(20));
	::testing::NiceMock<MockPwm> pwm;
	engineConfiguration->vvtActivationDelayMs = 500;
	VvtController early(0, 0, 0);
	early.init(&targetMap, &pwm);
	advanceTimeUs(250000);
	ASSERT_LT(engine->rpmCalculator.getSecondsSinceEngineStart(getTimeNowNt()), 0.5f);
	early.onFastCallback();
	early.setOutput(20);
	EXPECT_GT(early.vvtOutput, 0);

	// A whole-second delay remains effective at the same elapsed runtime.
	engineConfiguration->vvtActivationDelayMs = 1000;
	VvtController control(0, 0, 0);
	control.init(&targetMap, &pwm);
	control.onFastCallback();
	control.setOutput(20);
	EXPECT_EQ(control.vvtOutput, 0);
}

TEST(EngineControlReview, FailedPedalArmsAntiLagAndRaisesEtbFallback) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->antiLagEnabled = true;
	engineConfiguration->antiLagActivationMode = ALWAYS_ON_ANTILAG;
	engineConfiguration->ALSMaxTPS = 5;
	engineConfiguration->ALSEtbAdd = 20;

	Sensor::setMockValue(SensorType::Clt, 70);
	Sensor::setMockValue(SensorType::Rpm, 1000);
	Sensor::setMockValue(SensorType::BatteryVoltage, 12);
	Sensor::setMockValue(SensorType::DriverThrottleIntent, 20);
	Sensor::setMockValue(SensorType::AcceleratorPedal, 20, true);
	Sensor::setMockValue(SensorType::Tps1Primary, 0);
	Sensor::setMockValue(SensorType::Tps1, 20, true);

	MockVp3d pedalMap;
	EXPECT_CALL(pedalMap, getValue(_, _)).WillRepeatedly([](float, float pedal) { return pedal; });
	MockMotor motor;
	pid_s pid = {};
	pid.pFactor = 1;
	pid.minValue = -100;
	pid.maxValue = 100;
	setArrayValues(config->etbBiasValues, 0);
	EtbController etb;
	ASSERT_TRUE(etb.init(DC_Throttle1, &motor, &pid, &pedalMap, true));
	auto& sensorChecker = engine->module<SensorChecker>().unmock();
	sensorChecker.onIgnitionStateChanged(true);
	advanceTimeUs(6e6);
	sensorChecker.onSlowCallback();
	ASSERT_TRUE(sensorChecker.analogSensorsShouldWork());

	engine->antilagController.update();
	ASSERT_FALSE(engine->antilagController.isAntilagCondition);
	ASSERT_FLOAT_EQ(20, etb.getSetpoint().value_or(-1));

	Sensor::resetMockValue(SensorType::DriverThrottleIntent);
	Sensor::resetMockValue(SensorType::AcceleratorPedal);
	sensorChecker.onSlowCallback();
	ASSERT_TRUE(sensorChecker.analogSensorsShouldWork());
	ASSERT_FALSE(Sensor::get(SensorType::DriverThrottleIntent));
	ASSERT_FALSE(Sensor::get(SensorType::AcceleratorPedal));
	engine->antilagController.update();
	EXPECT_TRUE(engine->antilagController.isAntilagCondition);
	EXPECT_FLOAT_EQ(20, etb.getSetpoint().value_or(-1));
	// If the plate begins closing after the fault, the active controller
	// commands the motor to reopen it toward the anti-lag target.
	Sensor::setMockValue(SensorType::Tps1, 1, true);
	EXPECT_CALL(motor, enable());
	EXPECT_CALL(motor, set(Gt(0.0f)));
	etb.update();

	// The same failed pedal normally falls back to the minimum ETB target.
	engineConfiguration->antiLagEnabled = false;
	engine->antilagController.update();
	EXPECT_FLOAT_EQ(1, etb.getSetpoint().value_or(-1));
}

TEST(EngineControlReview, AntiLagContinuesPastConfiguredTimeout) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->antiLagEnabled = true;
	engineConfiguration->antiLagActivationMode = ALWAYS_ON_ANTILAG;
	engineConfiguration->ALSMaxDuration = 1;
	engineConfiguration->ALSMaxTPS = 5;
	Sensor::setMockValue(SensorType::Clt, 70);
	Sensor::setMockValue(SensorType::Rpm, 1000);
	Sensor::setMockValue(SensorType::DriverThrottleIntent, 0);

	engine->antilagController.update();
	ASSERT_TRUE(engine->antilagController.isAntilagCondition);
	advanceTimeUs(5e6);
	engine->antilagController.update();
	EXPECT_TRUE(engine->antilagController.isAntilagCondition);
	EXPECT_FALSE(engine->antilagController.ALSTimerCondition);
}

TEST(EngineControlReview, InactiveAntiLagStillSkipsSpark) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setCylinderCount(1);
	engineConfiguration->firingOrder = FO_1;
	engineConfiguration->isIgnitionEnabled = true;
	engineConfiguration->antiLagEnabled = false;
	engineConfiguration->ALSSkipRatio = 1;
	engine->ignitionState.sparkDwell = 1;
	engine->ignitionState.dwellAngle = 10;
	engineConfiguration->minimumIgnitionTiming = -25;
	engine->cylinders[0].setIgnitionTimingBtdc(-25);
	engine->rpmCalculator.oneDegreeUs = 100;
	ASSERT_FALSE(engine->antilagController.isAntilagCondition);

	::testing::StrictMock<MockExecutor> executor;
	engine->scheduler.setMockExecutor(&executor);
	EXPECT_CALL(executor, schedule(_, _, _, _)).Times(2);
	// First event installs the ALS skip ratio after testing it.
	onTriggerEventSparkLogic({1000000, 10, 30, 10, 30});
	// With anti-lag still disabled, the next eligible event is skipped.
	onTriggerEventSparkLogic({2000000, 10, 30, 10, 30});
}

TEST(EngineControlReview, ZeroAntiLagSkipRatioSchedulesBothSparks) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setCylinderCount(1);
	engineConfiguration->firingOrder = FO_1;
	engineConfiguration->isIgnitionEnabled = true;
	engineConfiguration->antiLagEnabled = false;
	engineConfiguration->ALSSkipRatio = 0;
	engine->ignitionState.sparkDwell = 1;
	engine->ignitionState.dwellAngle = 10;
	engineConfiguration->minimumIgnitionTiming = -25;
	engine->cylinders[0].setIgnitionTimingBtdc(-25);
	engine->rpmCalculator.oneDegreeUs = 100;

	::testing::StrictMock<MockExecutor> executor;
	engine->scheduler.setMockExecutor(&executor);
	EXPECT_CALL(executor, schedule(_, _, _, _)).Times(4);
	onTriggerEventSparkLogic({1000000, 10, 30, 10, 30});
	onTriggerEventSparkLogic({2000000, 10, 30, 10, 30});
}

TEST(EngineControlReview, IgnitionOffDoesNotCancelQueuedPrimeInjection) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setTimeNowUs(1e6);
	// A delayed prime leaves time for the ignition-off debounce to complete.
	engineConfiguration->primingDelay = 1;
	setArrayValues(engineConfiguration->primeValues, 50);
	Sensor::setMockValue(SensorType::Clt, 50);
	MockInjectorModel2 injectorModel;
	engine->module<InjectorModelPrimary>().set(&injectorModel);
	EXPECT_CALL(injectorModel, getInjectionDuration(_)).WillRepeatedly(::testing::Return(10.0f));
	auto& prime = engine->module<PrimeController>().unmock();

	prime.onIgnitionStateChanged(true);
	ASSERT_EQ(1, engine->scheduler.size());
	eth.moveTimeForwardAndInvokeEventsUs(250000);
	prime.onIgnitionStateChanged(false);
	eth.moveTimeForwardAndInvokeEventsUs(851000);
	EXPECT_TRUE(prime.isPriming());
	EXPECT_TRUE(enginePins.injectors[0].getLogicValue());

	eth.moveTimeForwardAndInvokeEventsUs(10000);
	EXPECT_FALSE(prime.isPriming());
	EXPECT_FALSE(enginePins.injectors[0].getLogicValue());
}

TEST(EngineControlReview, FuelPumpRemainsOnAfterIgnitionOffWhileTriggerRecent) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->fuelPumpPin = Gpio::A0;
	enginePins.fuelPumpRelay.init();
	FuelPumpController dut;
	setTimeNowUs(1e6);
	dut.onIgnitionStateChanged(true);
	advanceTimeUs(10e6);
	dut.onSlowCallback();
	ASSERT_FALSE(efiReadPin(Gpio::A0));

	engine->triggerCentral.handleShaftSignal(TriggerEvent::PrimaryFalling, getTimeNowNt());
	dut.onSlowCallback();
	ASSERT_TRUE(efiReadPin(Gpio::A0));
	dut.onIgnitionStateChanged(false);
	dut.onSlowCallback();
	EXPECT_FALSE(dut.ignitionOn);
	EXPECT_TRUE(efiReadPin(Gpio::A0));
}

TEST(EngineControlReview, FuelPumpKeepsPrimingAfterIgnitionOff) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->fuelPumpPin = Gpio::A0;
	engineConfiguration->startUpFuelPumpDuration = 4;
	enginePins.fuelPumpRelay.init();
	FuelPumpController dut;
	setTimeNowUs(1e6);
	dut.onIgnitionStateChanged(true);
	dut.onSlowCallback();
	ASSERT_TRUE(efiReadPin(Gpio::A0));

	dut.onIgnitionStateChanged(false);
	dut.onSlowCallback();
	EXPECT_FALSE(dut.ignitionOn);
	EXPECT_TRUE(efiReadPin(Gpio::A0));

	advanceTimeUs(5e6);
	dut.onSlowCallback();
	EXPECT_FALSE(efiReadPin(Gpio::A0));
}
