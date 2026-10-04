#include "pch.h"

#include "dc_motor.h"
#include "electronic_throttle_impl.h"
#include "pin_repository.h"

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
	OutputPin disable;
	TwoPinDcMotor motor{disable};
	RecordingPwm enable, dir1, dir2;
	MotorHardware() {
		motor.configure(enable, dir1, dir2, false);
		motor.setType(TwoPinDcMotor::ControlType::PwmEnablePin);
	}
	void expectStopped() const {
		EXPECT_TRUE(disable.getLogicValue());
		EXPECT_EQ(motor.get(), 0);
		EXPECT_EQ(enable.duty, 0);
		EXPECT_EQ(dir1.duty, 0);
		EXPECT_EQ(dir2.duty, 0);
	}
};

pid_s makePid() {
	pid_s pid = {};
	pid.pFactor = 1;
	pid.minValue = -90;
	pid.maxValue = 90;
	return pid;
}
} // namespace

TEST(DcReconfiguration, StopBothBridgeModesAndInvertedOutputsWithoutBatteryCompensation) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	for (auto type : {TwoPinDcMotor::ControlType::PwmDirectionPins, TwoPinDcMotor::ControlType::PwmEnablePin}) {
		for (bool inverted : {false, true}) {
			for (bool validBattery : {false, true}) {
				MotorHardware hw;
				hw.motor.setType(type);
				hw.motor.configure(hw.enable, hw.dir1, hw.dir2, inverted);
				hw.motor.set(0.6f);
				hw.motor.enable();
				if (validBattery) {
					Sensor::setMockValue(SensorType::BatteryVoltage, 0);
				} else {
					Sensor::setInvalidMockValue(SensorType::BatteryVoltage);
				}
				hw.motor.stop("test");
				EXPECT_EQ(hw.enable.duty, 0);
				EXPECT_EQ(hw.dir1.duty, inverted ? 1 : 0);
				EXPECT_EQ(hw.dir2.duty, inverted ? 1 : 0);
				EXPECT_EQ(hw.enable.immediateWrites, 1u);
				EXPECT_EQ(hw.dir1.immediateWrites, 1u);
				EXPECT_EQ(hw.dir2.immediateWrites, 1u);
				EXPECT_EQ(hw.motor.get(), 0);
				EXPECT_TRUE(hw.disable.getLogicValue());
				// Enabling alone cannot restore the old output.
				hw.motor.enable();
				EXPECT_EQ(hw.enable.duty, 0);
				Sensor::setMockValue(SensorType::BatteryVoltage, 14);
			}
		}
	}
}

TEST(DcReconfiguration, DisabledSlotStopsAndDetachesWithoutClaimingNewPins) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	MotorHardware hw;
	auto pid = makePid();
	EtbController controller;
	ASSERT_TRUE(controller.init(DC_Wastegate, &hw.motor, &pid, nullptr, false));
	controller.setOutput(50);
	ASSERT_GT(hw.enable.duty, 0);
	engine->etbControllers[0] = &controller;
	engine->etbControllers[1] = nullptr;
	engineConfiguration->etbFunctions[0] = DC_None;
	engineConfiguration->etbFunctions[1] = DC_None;
	// The disabled slot's configuration can contain pins owned by Lua.
	brain_pin_markUsed(Gpio::C1, "Lua");
	engineConfiguration->etbIo[0].controlPin = Gpio::C1;
	doInitElectronicThrottle();
	hw.expectStopped();
	EXPECT_STREQ(getBrainUsedPin(brainPin_to_index(Gpio::C1)), "Lua");
	controller.setOutput(50);
	controller.update();
	hw.expectStopped();
}

TEST(DcReconfiguration, DirectNoneInitializationStopsPreviousMotor) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	MotorHardware hw;
	auto pid = makePid();
	EtbController controller;
	ASSERT_TRUE(controller.init(DC_Wastegate, &hw.motor, &pid, nullptr, false));
	controller.setOutput(50);
	ASSERT_FALSE(controller.init(DC_None, &hw.motor, &pid, nullptr, false));
	controller.setOutput(50);
	hw.expectStopped();
}

enum class InitFailure {
	MissingPedal,
	MissingPrimaryTps,
	NonredundantTps,
	NonredundantPedal
};
class FailedDcReconfiguration : public testing::TestWithParam<InitFailure> {};

TEST_P(FailedDcReconfiguration, StopsPreviousMotorAndDetachesBothBindings) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	MotorHardware oldHardware, newHardware;
	auto pid = makePid();
	EtbController controller;
	ASSERT_TRUE(controller.init(DC_Wastegate, &oldHardware.motor, &pid, nullptr, false));
	controller.setOutput(50);
	Sensor::setMockValue(SensorType::Tps1Primary, 10);
	Sensor::setMockValue(SensorType::Tps1, 10, GetParam() != InitFailure::NonredundantTps);
	Sensor::setMockValue(SensorType::AcceleratorPedal, 30, GetParam() != InitFailure::NonredundantPedal);
	if (GetParam() == InitFailure::MissingPrimaryTps) {
		Sensor::resetMockValue(SensorType::Tps1Primary);
	}
	if (GetParam() == InitFailure::NonredundantTps || GetParam() == InitFailure::NonredundantPedal) {
		EXPECT_FATAL_ERROR(controller.init(DC_Throttle1, &newHardware.motor, &pid, nullptr, true));
	} else {
		ASSERT_FALSE(controller.init(
				DC_Throttle1, &newHardware.motor, &pid, nullptr, GetParam() != InitFailure::MissingPedal));
	}
	oldHardware.expectStopped();
	controller.setOutput(50);
	controller.update();
	oldHardware.expectStopped();
	EXPECT_EQ(newHardware.enable.immediateWrites, 0u);
	EXPECT_EQ(newHardware.enable.duty, 0);
	EXPECT_FLOAT_EQ(controller.m_outputDuty, 0);
}

INSTANTIATE_TEST_SUITE_P(
		ValidationFailures,
		FailedDcReconfiguration,
		testing::Values(
				InitFailure::MissingPedal,
				InitFailure::MissingPrimaryTps,
				InitFailure::NonredundantTps,
				InitFailure::NonredundantPedal));

TEST(DcReconfiguration, ValidReinitializationStopsOldMotorAndUsesNewCommand) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	MotorHardware oldHardware, newHardware, unaffectedHardware;
	auto pid = makePid();
	EtbController controller, unaffected;
	ASSERT_TRUE(controller.init(DC_Wastegate, &oldHardware.motor, &pid, nullptr, false));
	ASSERT_TRUE(unaffected.init(DC_Wastegate, &unaffectedHardware.motor, &pid, nullptr, false));
	controller.setOutput(50);
	unaffected.setOutput(30);
	ASSERT_TRUE(controller.init(DC_Wastegate, &newHardware.motor, &pid, nullptr, false));
	oldHardware.expectStopped();
	EXPECT_FLOAT_EQ(controller.m_outputDuty, 0);
	EXPECT_FLOAT_EQ(unaffectedHardware.motor.get(), 0.3f);
	EXPECT_EQ(unaffectedHardware.enable.immediateWrites, 0u);
	EXPECT_FALSE(unaffectedHardware.disable.getLogicValue());
	controller.setOutput(-25);
	EXPECT_FLOAT_EQ(newHardware.motor.get(), -0.25f);
	EXPECT_FLOAT_EQ(newHardware.enable.duty, 0.25f);
	EXPECT_FALSE(newHardware.disable.getLogicValue());
	oldHardware.expectStopped();
}

TEST(DcReconfiguration, ReinitializationAbortsAutocalibration) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	MotorHardware oldHardware, newHardware;
	auto pid = makePid();
	struct PedalMap : ValueProvider3D {
		float getValue(float, float pedal) const override {
			return pedal;
		}
	} map;
	Sensor::setMockValue(SensorType::Tps1Primary, 10);
	Sensor::setMockValue(SensorType::Tps1, 10, true);
	Sensor::setMockValue(SensorType::AcceleratorPedal, 30, true);
	initElectronicThrottle();
	auto controller = engine->etbControllers[0];
	ASSERT_NE(controller, nullptr);
	ASSERT_TRUE(controller->init(DC_Throttle1, &oldHardware.motor, &pid, &map, true));
	controller->autoCalibrateTps();
	controller->update();
	ASSERT_FLOAT_EQ(oldHardware.motor.get(), 0.5f);
	// Reusing the same controller for another throttle must not continue an
	// autocalibration sequence started on the old motor.
	ASSERT_TRUE(controller->init(DC_Throttle1, &newHardware.motor, &pid, &map, true));
	oldHardware.expectStopped();
	controller->update();
	EXPECT_NE(newHardware.motor.get(), 0.5f);
	EXPECT_GT(newHardware.enable.duty, 0);
	controller->deinit();
}
