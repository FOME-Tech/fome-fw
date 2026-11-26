/**
 * Settings changed by a burn (incrementGlobalConfigurationVersion) must take effect the same way they
 * would after a reboot with those settings.
 */

#include "pch.h"

// Changing only the firing order must rebuild the cylinder layout: count, and each cylinder's angle
TEST(ConfigChange, FiringOrderRebuildsCylinders) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	ASSERT_EQ(FO_1_3_4_2, engineConfiguration->firingOrder);
	ASSERT_EQ(4u, engine->engineState.cylinderCount);

	// Pretend an ignition schedule was built for the old layout
	engine->ignitionEvents.isReady = true;

	engineConfiguration->firingOrder = FO_1_5_3_6_2_4;
	incrementGlobalConfigurationVersion();

	EXPECT_EQ(6u, engine->engineState.cylinderCount);

	// The old schedule only has 4 cylinders set up, it has to be rebuilt
	EXPECT_FALSE(engine->ignitionEvents.isReady);

	// Each cylinder fires 120 degrees after the previous one in the firing order
	for (size_t i = 0; i < 6; i++) {
		size_t cylinder = getCylinderNumberAtIndex(i);
		EXPECT_NEAR(120.0f * i, engine->cylinders[cylinder].getAngleOffset(), 1e-3) << "index " << i;
	}
}

extern float getTachFreq();

// A tach pin assigned by a burn, rather than present at startup, drives the tach
TEST(ConfigChange, TachometerPinAssigned) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	ASSERT_FALSE(isBrainPinValid(engineConfiguration->tachOutputPin));

	engineConfiguration->tachPulsePerRev = 2;
	Sensor::setMockValue(SensorType::Rpm, 1500);

	engineConfiguration->tachOutputPin = Gpio::A8;
	incrementGlobalConfigurationVersion();

	engine->periodicFastCallback();

	// 1500 rpm, 2 pulses per rev
	EXPECT_NEAR(50, getTachFreq(), 1e-3);
}

// ...and removing it again stops it
TEST(ConfigChange, TachometerPinRemoved) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	engineConfiguration->tachPulsePerRev = 2;
	engineConfiguration->tachOutputPin = Gpio::A8;
	incrementGlobalConfigurationVersion();

	Sensor::setMockValue(SensorType::Rpm, 1500);
	engine->periodicFastCallback();
	ASSERT_NEAR(50, getTachFreq(), 1e-3);

	engineConfiguration->tachOutputPin = Gpio::Unassigned;
	incrementGlobalConfigurationVersion();

	// No longer updated
	Sensor::setMockValue(SensorType::Rpm, 3000);
	engine->periodicFastCallback();
	EXPECT_NEAR(50, getTachFreq(), 1e-3);
}

TEST(ConfigChange, ConfiguredPwmFollowsWanted) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	static SimplePwm pwm("test");
	static OutputPin pin;
	ConfiguredPwm dut(pwm);

	engine_configuration_s prev{};

	// Not wanted: nothing starts
	dut.startIfWanted(nullptr, false, "test", &pin, 100, 0);
	EXPECT_FALSE(dut.isRunning());

	dut.startIfWanted(&prev, true, "test", &pin, 100, 0);
	EXPECT_TRUE(dut.isRunning());

	// Still wanted: stays running
	dut.stopIfUnwanted(true);
	dut.startIfWanted(&prev, true, "test", &pin, 100, 0);
	EXPECT_TRUE(dut.isRunning());

	dut.stopIfUnwanted(false);
	EXPECT_FALSE(dut.isRunning());
}

TEST(ConfigChange, ConfiguredPwmRefusedFrequencyIsNotRunning) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	static SimplePwm pwm("test");
	static OutputPin pin;
	ConfiguredPwm dut(pwm);

	// startSimplePwm refuses this, so it must not count as running
	dut.startIfWanted(nullptr, true, "test", &pin, 0, 0);
	EXPECT_FALSE(dut.isRunning());

	// NAN starts paused, waiting for a frequency
	dut.startIfWanted(nullptr, true, "test", &pin, NAN, 0);
	EXPECT_TRUE(dut.isRunning());
}

// A per-cylinder timing offset changes the odd-fire decision, which is cached
TEST(ConfigChange, CylinderTimingOffsetUpdatesOddFire) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	ASSERT_FALSE(engine->engineState.useOddFireWastedSpark);

	engineConfiguration->timing_offset_cylinder[1] = 10;
	incrementGlobalConfigurationVersion();

	EXPECT_TRUE(engine->engineState.useOddFireWastedSpark);
}

#include "gppwm.h"

// An on-off GPPWM channel whose table always asks for 100%
static void setUpGppwmChannel(gppwm_channel& cfg, brain_pin_e pin, uint16_t frequency) {
	cfg.pin = pin;
	cfg.pwmFrequency = frequency;
	cfg.dutyIfError = 100;
	cfg.onAboveDuty = 50;
	cfg.offBelowDuty = 40;

	for (auto& row : cfg.table) {
		for (auto& cell : row) {
			cell = 100;
		}
	}
}

// A GPPWM pin assigned by a burn, rather than present at startup, is driven
TEST(ConfigChange, GppwmPinAssigned) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& pin = enginePins.gppwmPins[0];
	ASSERT_FALSE(pin.isInitialized());

	setUpGppwmChannel(engineConfiguration->gppwm[0], Gpio::G1, 0);
	incrementGlobalConfigurationVersion();
	updateGppwm();

	EXPECT_TRUE(pin.isInitialized());
	EXPECT_EQ(100, engine->outputChannels.gppwmOutput[0]);
	EXPECT_TRUE(pin.getLogicValue());
}

// ...and removing it again releases the pin and stops updating the channel
TEST(ConfigChange, GppwmPinRemoved) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& pin = enginePins.gppwmPins[0];

	setUpGppwmChannel(engineConfiguration->gppwm[0], Gpio::G1, 0);
	incrementGlobalConfigurationVersion();
	updateGppwm();
	ASSERT_EQ(100, engine->outputChannels.gppwmOutput[0]);

	engineConfiguration->gppwm[0].pin = Gpio::Unassigned;
	incrementGlobalConfigurationVersion();
	updateGppwm();

	EXPECT_FALSE(pin.isInitialized());
	EXPECT_EQ(0, engine->outputChannels.gppwmOutput[0]);
}

// Switching a channel from PWM to on-off (frequency 0) drives the pin directly
TEST(ConfigChange, GppwmPwmToOnOff) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& pin = enginePins.gppwmPins[0];

	setUpGppwmChannel(engineConfiguration->gppwm[0], Gpio::G1, 100);
	incrementGlobalConfigurationVersion();
	updateGppwm();

	// Whatever the PWM left it at: from here only the on-off logic drives it (time doesn't move)
	pin.setValue(false);

	engineConfiguration->gppwm[0].pwmFrequency = 0;
	incrementGlobalConfigurationVersion();
	updateGppwm();

	EXPECT_TRUE(pin.getLogicValue());
}
