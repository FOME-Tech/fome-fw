#include "pch.h"

#include "dc_motor.h"
#include "electronic_throttle_impl.h"
#include "proxy_sensor.h"
#include "redundant_sensor.h"
#include "stored_value_sensor.h"

namespace {
struct RecordingPwm : IPwm {
	float duty = 0;
	unsigned immediateWrites = 0;
	void setSimplePwmDutyCycle(float value) override {
		duty = value;
	}
	void setDutyImmediate(float value) override {
		++immediateWrites;
		duty = value;
	}
};

struct MotorHardware {
	OutputPin disablePin;
	TwoPinDcMotor motor{disablePin};
	RecordingPwm enable, dir1, dir2;

	MotorHardware() {
		motor.configure(enable, dir1, dir2, false);
		motor.setType(TwoPinDcMotor::ControlType::PwmEnablePin);
	}
};

struct PedalMap : ValueProvider3D {
	float getValue(float, float pedal) const override {
		return pedal;
	}
};

pid_s makePid() {
	pid_s pid = {};
	pid.pFactor = 1;
	pid.minValue = -90;
	pid.maxValue = 90;
	return pid;
}

void setThrottleSensors() {
	Sensor::setMockValue(SensorType::Tps1Primary, 10);
	Sensor::setMockValue(SensorType::Tps2Primary, 10);
	Sensor::setMockValue(SensorType::Tps1, 10, true);
	Sensor::setMockValue(SensorType::Tps2, 10, true);
	Sensor::setMockValue(SensorType::AcceleratorPedal, 30, true);
	Sensor::setMockValue(SensorType::WastegatePosition, 10);
}

void attach(IEtbController& controller) {
	engine->etbControllers[0] = &controller;
	engine->etbControllers[1] = nullptr;
}
} // namespace

TEST(FlashActuatorSafety, MotorLatchStopsBothBridgeModesAndInvertedOutputs) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	for (auto type : {TwoPinDcMotor::ControlType::PwmDirectionPins, TwoPinDcMotor::ControlType::PwmEnablePin}) {
		for (bool inverted : {false, true}) {
			MotorHardware hw;
			hw.motor.setType(type);
			hw.motor.configure(hw.enable, hw.dir1, hw.dir2, inverted);
			hw.motor.set(0.6f);
			hw.motor.enable();
			// Even pathological voltage must not affect the physical stop path.
			Sensor::setMockValue(SensorType::BatteryVoltage, 0);
			hw.motor.setFlashInhibited(true);
			EXPECT_EQ(hw.enable.duty, 0);
			EXPECT_EQ(hw.dir1.duty, inverted ? 1 : 0);
			EXPECT_EQ(hw.dir2.duty, inverted ? 1 : 0);
			EXPECT_EQ(hw.enable.immediateWrites, 1u);
			EXPECT_EQ(hw.dir1.immediateWrites, 1u);
			EXPECT_EQ(hw.dir2.immediateWrites, 1u);
			EXPECT_EQ(hw.motor.get(), 0);
			EXPECT_TRUE(hw.disablePin.getLogicValue());

			hw.motor.enable();
			hw.motor.set(-0.8f);
			EXPECT_EQ(hw.motor.get(), 0);
			EXPECT_EQ(hw.enable.duty, 0);
			EXPECT_TRUE(hw.disablePin.getLogicValue());
			hw.motor.setFlashInhibited(false);
			EXPECT_EQ(hw.enable.duty, 0);
			EXPECT_TRUE(hw.disablePin.getLogicValue());
			Sensor::setMockValue(SensorType::BatteryVoltage, 14);
		}
	}
}

TEST(FlashActuatorSafety, SoftwarePwmStopsBeforeItsNextTimerCallback) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	OutputPin pin;
	SimplePwm pwm;
	startSimplePwm(&pwm, "flash test", &pin, 800, 0.6f);
	pwm.setDutyImmediate(0);
	EXPECT_FALSE(pin.getLogicValue());
	pwm.togglePwmState();
	EXPECT_FALSE(pin.getLogicValue());
	pwm.setDutyImmediate(1);
	EXPECT_TRUE(pin.getLogicValue());
	pwm.togglePwmState();
	EXPECT_TRUE(pin.getLogicValue());
	pwm.stop();
	pwm.setDutyImmediate(0);
	EXPECT_FALSE(pin.getLogicValue());
	eth.clearQueue();
}

class FlashActuatorModes : public testing::TestWithParam<dc_function_e> {};

TEST_P(FlashActuatorModes, ResumeRequiresFreshSensorsAndANewValidControlCycle) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	MotorHardware hw;
	PedalMap map;
	auto pid = makePid();
	EtbController controller;
	setThrottleSensors();
	ASSERT_TRUE(controller.init(GetParam(), &hw.motor, &pid, &map, true));
	controller.setWastegatePosition(30);
	attach(controller);
	controller.setOutput(60);
	ASSERT_FALSE(hw.disablePin.getLogicValue());
	beginBlockingFlash();
	EXPECT_TRUE(hw.disablePin.getLogicValue());
	EXPECT_EQ(hw.enable.duty, 0);
	controller.update();
	controller.setOutput(60);
	hw.motor.set(0.7f);
	hw.motor.enable();
	EXPECT_EQ(hw.enable.duty, 0);

	eth.moveTimeForwardMs(500);
	endBlockingFlash();
	// Timeout inhibition makes the old sample valid, but cannot make it fresh.
	Sensor::inhibitTimeouts(true);
	controller.update();
	EXPECT_TRUE(hw.disablePin.getLogicValue());
	eth.moveTimeForwardMs(1);
	setThrottleSensors();
	controller.setOutput(60);
	EXPECT_TRUE(hw.disablePin.getLogicValue());
	controller.update();
	EXPECT_FALSE(hw.disablePin.getLogicValue());
	EXPECT_GT(hw.enable.duty, 0);
	Sensor::inhibitTimeouts(false);

	// A second burn requires another set of samples, even after successful recovery.
	beginBlockingFlash();
	endBlockingFlash();
	controller.update();
	EXPECT_TRUE(hw.disablePin.getLogicValue());
}

INSTANTIATE_TEST_SUITE_P(AllDcFunctions, FlashActuatorModes, testing::Values(DC_Throttle1, DC_Throttle2, DC_Wastegate));

TEST(FlashActuatorSafety, InvalidFreshFeedbackKeepsWastegateDisabledAndResetsPidOnRecovery) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	MotorHardware hw;
	auto pid = makePid();
	pid.iFactor = 5;
	pid.dFactor = 1;
	EtbController controller;
	ASSERT_TRUE(controller.init(DC_Wastegate, &hw.motor, &pid, nullptr, false));
	attach(controller);
	controller.setWastegatePosition(50);
	Sensor::setMockValue(SensorType::WastegatePosition, 30);
	for (unsigned i = 0; i < 5; ++i) {
		controller.update();
	}
	ASSERT_GT(controller.getPidState().iTerm, 0.9f);
	beginBlockingFlash();
	eth.moveTimeForwardMs(1000);
	endBlockingFlash();
	eth.moveTimeForwardMs(1);
	Sensor::setInvalidMockValue(SensorType::WastegatePosition);
	controller.update();
	EXPECT_TRUE(hw.disablePin.getLogicValue());
	Sensor::setMockValue(SensorType::WastegatePosition, 10);
	controller.update();
	EXPECT_FALSE(hw.disablePin.getLogicValue());
	EXPECT_NEAR(controller.getPidState().iTerm, 0.4f, 0.0001f);
	EXPECT_EQ(controller.getPidState().dTerm, 0);
}

TEST(FlashActuatorSafety, FatalFirmwareErrorKeepsRecoveryPhysicallyInhibited) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	MotorHardware hw;
	auto pid = makePid();
	EtbController controller;
	ASSERT_TRUE(controller.init(DC_Wastegate, &hw.motor, &pid, nullptr, false));
	attach(controller);
	controller.setWastegatePosition(50);
	beginBlockingFlash();
	endBlockingFlash();
	eth.moveTimeForwardMs(1);
	Sensor::setMockValue(SensorType::WastegatePosition, 10);
	// Host firmwareError throws. Set its production latch directly and restore
	// the global state when the test leaves scope, including on an assertion.
	struct FirmwareErrorGuard {
		bool previous = hasFirmwareErrorFlag;
		FirmwareErrorGuard() {
			hasFirmwareErrorFlag = true;
		}
		~FirmwareErrorGuard() {
			hasFirmwareErrorFlag = previous;
		}
	} firmwareErrorGuard;
	getLimpManager()->fatalError();
	controller.update();
	controller.setOutput(50);
	hw.motor.set(0.5f);
	hw.motor.enable();
	EXPECT_TRUE(hw.disablePin.getLogicValue());
	EXPECT_EQ(hw.motor.get(), 0);
	EXPECT_EQ(hw.enable.duty, 0);
}

TEST(FlashActuatorSafety, ComputationStartedBeforeBurnCannotRestoreOldCommand) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	struct InterruptedController : EtbController {
		bool interrupt = true;
		expected<float> getOpenLoop(float) override {
			if (interrupt) {
				interrupt = false;
				beginBlockingFlash();
				endBlockingFlash();
			}
			return 0;
		}
	} controller;
	MotorHardware hw;
	auto pid = makePid();
	ASSERT_TRUE(controller.init(DC_Wastegate, &hw.motor, &pid, nullptr, false));
	attach(controller);
	controller.setWastegatePosition(50);
	Sensor::setMockValue(SensorType::WastegatePosition, 10);
	controller.update();
	EXPECT_TRUE(hw.disablePin.getLogicValue());
	EXPECT_EQ(hw.enable.duty, 0);
	eth.moveTimeForwardMs(1);
	Sensor::setMockValue(SensorType::WastegatePosition, 10);
	controller.update();
	EXPECT_FALSE(hw.disablePin.getLogicValue());
}

TEST(FlashActuatorSafety, AutocalibrationAndAutotuneAreAbortedByBurn) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	MotorHardware hw;
	PedalMap map;
	auto pid = makePid();
	setThrottleSensors();
	initElectronicThrottle();
	auto controller = engine->etbControllers[0];
	ASSERT_NE(controller, nullptr);
	ASSERT_TRUE(controller->init(DC_Throttle1, &hw.motor, &pid, &map, true));
	attach(*controller);
	controller->autoCalibrateTps();
	controller->update();
	ASSERT_FLOAT_EQ(hw.motor.get(), 0.5f);
	engine->etbAutoTune = true;
	beginBlockingFlash();
	EXPECT_FALSE(engine->etbAutoTune);
	controller->autoCalibrateTps();
	controller->update();
	EXPECT_EQ(hw.motor.get(), 0);
	eth.moveTimeForwardMs(1500);
	endBlockingFlash();
	controller->autoCalibrateTps();
	controller->update();
	EXPECT_EQ(hw.motor.get(), 0);
	eth.moveTimeForwardMs(1);
	setThrottleSensors();
	controller->update();
	EXPECT_FALSE(hw.disablePin.getLogicValue());
	EXPECT_NE(hw.motor.get(), 0.5f);
	controller->update();
	EXPECT_NE(hw.motor.get(), 0.5f);
	// The implementation is static: do not leave pointers to this test's objects.
	controller->init(DC_Wastegate, nullptr, nullptr, nullptr, false);
}

TEST(FlashActuatorSafety, SensorFreshnessFollowsRequiredLeavesAndMockOverrides) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	StoredValueSensor primary(SensorType::Tps1Primary, MS2NT(10));
	StoredValueSensor secondary(SensorType::Tps1Secondary, MS2NT(10));
	RedundantSensor redundant(SensorType::Tps1, SensorType::Tps1Primary, SensorType::Tps1Secondary);
	ProxySensor proxy(SensorType::DriverThrottleIntent);
	ASSERT_TRUE(primary.Register());
	ASSERT_TRUE(secondary.Register());
	ASSERT_TRUE(redundant.Register());
	ASSERT_TRUE(proxy.Register());
	proxy.setProxiedSensor(SensorType::Tps1);
	redundant.configure(5, false);
	auto finishedAt = getTimeNowNt();
	primary.setValidValue(10, finishedAt);
	secondary.setValidValue(10, finishedAt);
	EXPECT_FALSE(Sensor::hasUpdatedAfter(SensorType::Tps1, finishedAt));
	eth.moveTimeForwardMs(1);
	primary.setValidValue(10, getTimeNowNt());
	EXPECT_FALSE(Sensor::hasUpdatedAfter(SensorType::Tps1, finishedAt));
	redundant.configure(5, true);
	EXPECT_TRUE(Sensor::hasUpdatedAfter(SensorType::Tps1, finishedAt));
	redundant.configure(5, false);
	secondary.setValidValue(10, getTimeNowNt());
	EXPECT_TRUE(Sensor::hasUpdatedAfter(SensorType::DriverThrottleIntent, finishedAt));

	Sensor::setMockValue(SensorType::Tps1, 10, true);
	auto secondBurn = getTimeNowNt();
	eth.moveTimeForwardMs(1);
	primary.setValidValue(10, getTimeNowNt());
	secondary.setValidValue(10, getTimeNowNt());
	// Fresh underlying analog values do not freshen an overridden source.
	EXPECT_FALSE(Sensor::hasUpdatedAfter(SensorType::Tps1, secondBurn));
	Sensor::setMockValue(SensorType::Tps1, 10, true);
	EXPECT_TRUE(Sensor::hasUpdatedAfter(SensorType::Tps1, secondBurn));
	Sensor::setInvalidMockValue(SensorType::Tps1);
	EXPECT_FALSE(Sensor::hasUpdatedAfter(SensorType::Tps1, secondBurn));
	Sensor::resetMockValue(SensorType::Tps1);
	secondary.invalidate();
	EXPECT_FALSE(Sensor::hasUpdatedAfter(SensorType::Tps1, secondBurn));
}

TEST(FlashActuatorSafety, SensorWithoutTimestampsCannotAuthorizeRecovery) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	struct UntimedSensor : Sensor {
		UntimedSensor()
			: Sensor(SensorType::WastegatePosition) {}
		SensorResult get() const override {
			return 20;
		}
		void showInfo(const char*) const override {}
	} sensor;
	ASSERT_TRUE(sensor.Register());
	EXPECT_TRUE(Sensor::get(SensorType::WastegatePosition).Valid);
	EXPECT_FALSE(Sensor::hasUpdatedAfter(SensorType::WastegatePosition, 0));
}

TEST(FlashActuatorSafety, DisabledConfigurationCannotRecoverPreviousMotor) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	MotorHardware hw;
	auto pid = makePid();
	EtbController controller;
	ASSERT_TRUE(controller.init(DC_Wastegate, &hw.motor, &pid, nullptr, false));
	attach(controller);
	controller.setOutput(50);
	beginBlockingFlash();
	endBlockingFlash();
	engineConfiguration->etbFunctions[0] = DC_None;
	engineConfiguration->etbFunctions[1] = DC_None;
	doInitElectronicThrottle();
	eth.moveTimeForwardMs(1);
	Sensor::setMockValue(SensorType::WastegatePosition, 10);
	controller.setOutput(50);
	controller.update();
	hw.motor.enable();
	hw.motor.set(0.5f);
	EXPECT_TRUE(hw.disablePin.getLogicValue());
	EXPECT_EQ(hw.motor.get(), 0);
	EXPECT_EQ(hw.enable.duty, 0);
}

TEST(FlashActuatorSafety, FailedInitializationDropsPreviousBindingDuringRecovery) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	MotorHardware hw;
	PedalMap map;
	auto pid = makePid();
	EtbController controller;
	ASSERT_TRUE(controller.init(DC_Wastegate, &hw.motor, &pid, nullptr, false));
	attach(controller);
	controller.setOutput(50);
	beginBlockingFlash();
	endBlockingFlash();
	// The primary throttle sensor is not configured yet, so initialization fails.
	ASSERT_FALSE(controller.init(DC_Throttle1, &hw.motor, &pid, &map, true));
	eth.moveTimeForwardMs(1);
	setThrottleSensors();
	controller.setOutput(50);
	controller.update();
	EXPECT_TRUE(hw.disablePin.getLogicValue());
	EXPECT_EQ(hw.motor.get(), 0);
}

TEST(FlashActuatorSafety, ValidReinitializationPreservesLatchAndRequiresAnotherSensorSample) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	MotorHardware hw;
	auto pid = makePid();
	EtbController controller;
	ASSERT_TRUE(controller.init(DC_Wastegate, &hw.motor, &pid, nullptr, false));
	attach(controller);
	beginBlockingFlash();
	endBlockingFlash();
	eth.moveTimeForwardMs(1);
	Sensor::setMockValue(SensorType::WastegatePosition, 10);
	ASSERT_TRUE(controller.init(DC_Wastegate, &hw.motor, &pid, nullptr, false));
	controller.setWastegatePosition(50);
	controller.update();
	EXPECT_TRUE(hw.disablePin.getLogicValue());
	eth.moveTimeForwardMs(1);
	Sensor::setMockValue(SensorType::WastegatePosition, 10);
	controller.update();
	EXPECT_FALSE(hw.disablePin.getLogicValue());
	EXPECT_GT(hw.enable.duty, 0);
}
