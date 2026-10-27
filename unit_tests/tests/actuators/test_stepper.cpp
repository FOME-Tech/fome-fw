#include "pch.h"

#include "stepper.h"

namespace {
std::function<void(int)> onStepperPause;
}

// Observe GPIO state at each thread sleep without waiting in real time.
void StepperHw::waitMicroseconds(int us) const {
	if (onStepperPause) {
		onStepperPause(us);
	}
}

TEST(Stepper, IgnoreSmallChanges) {
	StepperMotorBase dut;

	dut.setTargetPosition(10);

	// Record initial reported position
	auto initialPosition = dut.getTargetPosition();

	// Small changes should be ignored
	dut.setTargetPosition(10.5f);
	EXPECT_EQ(initialPosition, dut.getTargetPosition());
	dut.setTargetPosition(9.5f);
	EXPECT_EQ(initialPosition, dut.getTargetPosition());

	// Change of >= 1 should cause a change
	dut.setTargetPosition(11.5f);
	EXPECT_EQ(11.5f, dut.getTargetPosition());

	// Now go back the other way
	dut.setTargetPosition(9.5f);
	EXPECT_EQ(9.5f, dut.getTargetPosition());
}

class StepDirectionStepperTest : public testing::Test {
protected:
	void SetUp() override {
		setTimeNowUs(0);
		enginePins.mainRelay.setValue(true);
		Sensor::setMockValue(SensorType::BatteryVoltage, 12);
		initialize();
		onStepperPause = [this](int us) { pauses.push_back({!isDisabled(), efiReadPin(Gpio::F8), us}); };
	}

	void TearDown() override {
		onStepperPause = nullptr;
	}

	void initialize(pin_output_mode_e enableMode = OM_DEFAULT, float reactionTime = 5) {
		dut.initialize(Gpio::F8, Gpio::F7, OM_DEFAULT, reactionTime, Gpio::F9, enableMode);
	}

	bool isDisabled() const {
		return efiReadPin(Gpio::F9);
	}

	EngineTestHelper eth{engine_type_e::TEST_ENGINE};
	StepDirectionStepper dut;
	struct PauseState {
		bool enabled;
		bool stepHigh;
		int durationUs;
	};
	std::vector<PauseState> pauses;
};

TEST_F(StepDirectionStepperTest, WaitsAfterEachEnableBeforeStep) {
	// No direction change, so the first pause must be the EN settling delay.
	ASSERT_TRUE(dut.step(false));
	ASSERT_EQ(3u, pauses.size());
	EXPECT_TRUE(pauses[0].enabled);
	EXPECT_FALSE(pauses[0].stepHigh);
	EXPECT_EQ(2000, pauses[0].durationUs);
	EXPECT_TRUE(pauses[1].enabled);
	EXPECT_TRUE(pauses[1].stepHigh);
	EXPECT_TRUE(pauses[2].enabled);
	EXPECT_FALSE(pauses[2].stepHigh);

	// An already enabled driver needs only the HIGH and LOW pulse pauses.
	pauses.clear();
	ASSERT_TRUE(dut.step(false));
	ASSERT_EQ(2u, pauses.size());
	EXPECT_TRUE(pauses[0].enabled);
	EXPECT_TRUE(pauses[0].stepHigh);
	EXPECT_TRUE(pauses[1].enabled);
	EXPECT_FALSE(pauses[1].stepHigh);

	setTimeNowUs(5000001);
	dut.sleep();
	ASSERT_TRUE(isDisabled());
	pauses.clear();
	ASSERT_TRUE(dut.step(false));
	ASSERT_EQ(3u, pauses.size());
	EXPECT_TRUE(pauses[0].enabled);
	EXPECT_FALSE(pauses[0].stepHigh);
	EXPECT_EQ(2000, pauses[0].durationUs);
	EXPECT_TRUE(pauses[1].enabled);
	EXPECT_TRUE(pauses[1].stepHigh);
}

TEST_F(StepDirectionStepperTest, EnableDelayIsIndependentOfReactionTime) {
	for (float reactionTime : {1.0f, 20.0f, 300.0f}) {
		initialize(OM_DEFAULT, reactionTime);
		pauses.clear();
		ASSERT_TRUE(dut.step(false));
		ASSERT_EQ(3u, pauses.size());
		EXPECT_TRUE(pauses[0].enabled);
		EXPECT_FALSE(pauses[0].stepHigh);
		EXPECT_EQ(2000, pauses[0].durationUs);
		EXPECT_EQ(MS2US(reactionTime), pauses[1].durationUs);
		EXPECT_EQ(MS2US(reactionTime), pauses[2].durationUs);
	}
}

TEST_F(StepDirectionStepperTest, HoldsBetweenStepsAndReleasesAfterTimeout) {
	EXPECT_TRUE(isDisabled());
	EXPECT_FALSE(efiReadPin(Gpio::F8));
	ASSERT_TRUE(dut.step(true));
	EXPECT_FALSE(isDisabled());
	EXPECT_FALSE(efiReadPin(Gpio::F8));
	EXPECT_TRUE(efiReadPin(Gpio::F7));

	setTimeNowUs(4999999);
	dut.sleep();
	EXPECT_FALSE(isDisabled());

	setTimeNowUs(5000001);
	dut.sleep();
	EXPECT_TRUE(isDisabled());
	EXPECT_FALSE(efiReadPin(Gpio::F8));
}

TEST_F(StepDirectionStepperTest, FurtherStepsRestartTimeout) {
	ASSERT_TRUE(dut.step(true));
	setTimeNowUs(4000000);
	ASSERT_TRUE(dut.step(false));
	EXPECT_FALSE(isDisabled());
	EXPECT_FALSE(efiReadPin(Gpio::F7));

	setTimeNowUs(8000000);
	dut.sleep();
	EXPECT_FALSE(isDisabled());

	setTimeNowUs(9000001);
	dut.sleep();
	EXPECT_TRUE(isDisabled());
}

TEST_F(StepDirectionStepperTest, ResumesAfterTimeout) {
	ASSERT_TRUE(dut.step(true));
	setTimeNowUs(5000001);
	dut.sleep();
	ASSERT_TRUE(isDisabled());

	ASSERT_TRUE(dut.step(false));
	EXPECT_FALSE(isDisabled());
	EXPECT_FALSE(efiReadPin(Gpio::F8));
	EXPECT_FALSE(efiReadPin(Gpio::F7));
	dut.sleep();
	EXPECT_FALSE(isDisabled());
}

TEST_F(StepDirectionStepperTest, ReinitializationDisablesDriver) {
	ASSERT_TRUE(dut.step(true));
	initialize();
	EXPECT_TRUE(isDisabled());
	EXPECT_FALSE(efiReadPin(Gpio::F8));
	EXPECT_FALSE(efiReadPin(Gpio::F7));
}

TEST_F(StepDirectionStepperTest, InvertedEnableMode) {
	initialize(OM_INVERTED);
	EXPECT_FALSE(efiReadPin(Gpio::F9));
	ASSERT_TRUE(dut.step(true));
	EXPECT_TRUE(efiReadPin(Gpio::F9));
	setTimeNowUs(5000001);
	dut.sleep();
	EXPECT_FALSE(efiReadPin(Gpio::F9));
}

TEST_F(StepDirectionStepperTest, NoStepWithoutMainRelay) {
	ASSERT_TRUE(dut.step(true));
	enginePins.mainRelay.setValue(false);
	EXPECT_FALSE(dut.step(true));
	EXPECT_TRUE(isDisabled());
	EXPECT_FALSE(efiReadPin(Gpio::F8));
}

TEST_F(StepDirectionStepperTest, NoStepIfMainRelayTurnsOffDuringEnableDelay) {
	onStepperPause = [this](int) {
		EXPECT_FALSE(isDisabled());
		EXPECT_FALSE(efiReadPin(Gpio::F8));
		enginePins.mainRelay.setValue(false);
	};
	EXPECT_FALSE(dut.step(false));
	EXPECT_TRUE(isDisabled());
	EXPECT_FALSE(efiReadPin(Gpio::F8));
}

TEST_F(StepDirectionStepperTest, MotorReleasesDriverWhenPowerIsUnavailable) {
	StepperMotorBase motor;
	motor.initialize(&dut, 100);
	engineConfiguration->stepperForceParkingEveryRestart = false;
	engineConfiguration->minStepperVoltage = 10;
	motor.doIteration();
	motor.setTargetPosition(10);
	motor.doIteration();
	ASSERT_EQ(1, motor.m_currentPosition);
	ASSERT_FALSE(isDisabled());

	Sensor::setMockValue(SensorType::BatteryVoltage, 9);
	motor.doIteration();
	EXPECT_TRUE(isDisabled());
	EXPECT_EQ(1, motor.m_currentPosition);

	Sensor::setMockValue(SensorType::BatteryVoltage, 12);
	motor.doIteration();
	EXPECT_FALSE(isDisabled());
	EXPECT_EQ(2, motor.m_currentPosition);

	enginePins.mainRelay.setValue(false);
	motor.doIteration();
	EXPECT_TRUE(isDisabled());
	EXPECT_EQ(2, motor.m_currentPosition);
}

TEST_F(StepDirectionStepperTest, MotorHoldsTargetUntilTimeout) {
	StepperMotorBase motor;
	motor.initialize(&dut, 100);
	engineConfiguration->stepperForceParkingEveryRestart = false;
	motor.doIteration();
	motor.setTargetPosition(1);
	motor.doIteration();
	ASSERT_EQ(1, motor.m_currentPosition);
	ASSERT_TRUE(motor.isBusy());

	motor.doIteration();
	EXPECT_FALSE(motor.isBusy());
	EXPECT_FALSE(isDisabled());

	setTimeNowUs(5000001);
	motor.doIteration();
	EXPECT_TRUE(isDisabled());
	EXPECT_EQ(1, motor.m_currentPosition);
}
