#include "pch.h"

#include "clutch_state.h"

namespace {
const expected<bool> noSwitch = unexpected;

bool hasWarning(EngineTestHelper& eth, ObdCode code) {
	auto* warnings = eth.recentWarnings();
	for (size_t i = 0; i < warnings->getCount(); i++) {
		if (warnings->get(i).Code == code) {
			return true;
		}
	}

	return false;
}
} // namespace

TEST(ClutchState, Analog) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	engineConfiguration->clutchAppliedBelow = 15;
	engineConfiguration->clutchReleasedAbove = 60;

	EXPECT_EQ(ClutchState::Indeterminate, decideClutchState(unexpected, noSwitch, noSwitch));

	EXPECT_EQ(ClutchState::Engaged, decideClutchState(0, noSwitch, noSwitch));
	EXPECT_EQ(ClutchState::Engaged, decideClutchState(14.9f, noSwitch, noSwitch));
	EXPECT_EQ(ClutchState::Middle, decideClutchState(15, noSwitch, noSwitch));
	EXPECT_EQ(ClutchState::Middle, decideClutchState(60, noSwitch, noSwitch));
	EXPECT_EQ(ClutchState::Disengaged, decideClutchState(60.1f, noSwitch, noSwitch));
	EXPECT_EQ(ClutchState::Disengaged, decideClutchState(100, noSwitch, noSwitch));

	// Thresholds the wrong way round are unusable
	engineConfiguration->clutchAppliedBelow = 60;
	engineConfiguration->clutchReleasedAbove = 15;
	EXPECT_EQ(ClutchState::Indeterminate, decideClutchState(0, noSwitch, noSwitch));
}

TEST(ClutchState, AnalogWinsOverSwitches) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	engineConfiguration->clutchAppliedBelow = 15;
	engineConfiguration->clutchReleasedAbove = 60;

	// Switches disagree with the sensor: the sensor wins
	EXPECT_EQ(ClutchState::Engaged, decideClutchState(0, false, true));
	EXPECT_EQ(ClutchState::Middle, decideClutchState(40, true, false));
	EXPECT_EQ(ClutchState::Disengaged, decideClutchState(100, true, false));

	// Even switches that are both on are ignored while the sensor is good
	EXPECT_EQ(ClutchState::Engaged, decideClutchState(0, true, true));
	EXPECT_FALSE(hasWarning(eth, ObdCode::OBD_Clutch_Switch_Input));

	// Sensor fault: fall back to the switches
	EXPECT_EQ(ClutchState::Disengaged, decideClutchState(unexpected, false, true));

	// Unusable thresholds: fall back to the switches
	engineConfiguration->clutchAppliedBelow = 0;
	engineConfiguration->clutchReleasedAbove = 0;
	EXPECT_EQ(ClutchState::Engaged, decideClutchState(50, true, false));
}

TEST(ClutchState, Switches) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	// Both switches
	EXPECT_EQ(ClutchState::Engaged, decideClutchState(unexpected, true, false));
	EXPECT_EQ(ClutchState::Middle, decideClutchState(unexpected, false, false));
	EXPECT_EQ(ClutchState::Disengaged, decideClutchState(unexpected, false, true));

	// Up switch only
	EXPECT_EQ(ClutchState::Engaged, decideClutchState(unexpected, true, noSwitch));
	EXPECT_EQ(ClutchState::MiddleOrDisengaged, decideClutchState(unexpected, false, noSwitch));

	// Down switch only
	EXPECT_EQ(ClutchState::Disengaged, decideClutchState(unexpected, noSwitch, true));
	EXPECT_EQ(ClutchState::EngagedOrMiddle, decideClutchState(unexpected, noSwitch, false));

	// Nothing
	EXPECT_EQ(ClutchState::Indeterminate, decideClutchState(unexpected, noSwitch, noSwitch));

	EXPECT_FALSE(hasWarning(eth, ObdCode::OBD_Clutch_Switch_Input));
}

TEST(ClutchState, BothSwitchesOnIsFault) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	EXPECT_EQ(ClutchState::Indeterminate, decideClutchState(unexpected, true, true));
	EXPECT_TRUE(hasWarning(eth, ObdCode::OBD_Clutch_Switch_Input));
}

TEST(ClutchState, Predicates) {
	EXPECT_FALSE(isClutchKnownNotEngaged(ClutchState::Indeterminate));
	EXPECT_FALSE(isClutchKnownNotEngaged(ClutchState::Engaged));
	EXPECT_TRUE(isClutchKnownNotEngaged(ClutchState::Middle));
	EXPECT_TRUE(isClutchKnownNotEngaged(ClutchState::Disengaged));
	EXPECT_FALSE(isClutchKnownNotEngaged(ClutchState::EngagedOrMiddle));
	EXPECT_TRUE(isClutchKnownNotEngaged(ClutchState::MiddleOrDisengaged));

	EXPECT_FALSE(isClutchKnownDisengaged(ClutchState::Indeterminate));
	EXPECT_FALSE(isClutchKnownDisengaged(ClutchState::Engaged));
	EXPECT_FALSE(isClutchKnownDisengaged(ClutchState::Middle));
	EXPECT_TRUE(isClutchKnownDisengaged(ClutchState::Disengaged));
	EXPECT_FALSE(isClutchKnownDisengaged(ClutchState::EngagedOrMiddle));
	EXPECT_FALSE(isClutchKnownDisengaged(ClutchState::MiddleOrDisengaged));
}

TEST(ClutchState, ReadFromInputs) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	Sensor::setInvalidMockValue(SensorType::ClutchPosition);

	// Nothing configured
	engine->updateSwitchInputs();
	EXPECT_EQ(ClutchState::Indeterminate, getClutchState());
	EXPECT_FALSE(engine->engineState.clutchUpState);
	EXPECT_FALSE(engine->engineState.clutchDownState);

	// Both switches, with inversion
	engineConfiguration->clutchUpPin = Gpio::G2;
	engineConfiguration->clutchDownPin = Gpio::G3;
	engineConfiguration->clutchDownPinInverted = true;
	setMockState(Gpio::G2, false);
	setMockState(Gpio::G3, false);
	engine->updateSwitchInputs();
	EXPECT_EQ(ClutchState::Disengaged, getClutchState());
	EXPECT_FALSE(engine->engineState.clutchUpState);
	EXPECT_TRUE(engine->engineState.clutchDownState);

	// Removing the down pin clears its state rather than leaving it stale
	engineConfiguration->clutchDownPin = Gpio::Unassigned;
	engine->updateSwitchInputs();
	EXPECT_EQ(ClutchState::MiddleOrDisengaged, getClutchState());
	EXPECT_FALSE(engine->engineState.clutchDownState);

	// The analog sensor wins over the switches
	engineConfiguration->clutchAppliedBelow = 15;
	engineConfiguration->clutchReleasedAbove = 60;
	Sensor::setMockValue(SensorType::ClutchPosition, 5);
	engine->updateSwitchInputs();
	EXPECT_EQ(ClutchState::Engaged, getClutchState());
}

TEST(ClutchState, LuaUpSwitch) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	Sensor::setInvalidMockValue(SensorType::ClutchPosition);

	// Lua's default state isn't a source until a script sets it
	engine->updateSwitchInputs();
	EXPECT_EQ(ClutchState::Indeterminate, getClutchState());

	engine->engineState.lua.clutchUpStateSet = true;
	engine->engineState.lua.clutchUpState = true;
	engine->updateSwitchInputs();
	EXPECT_EQ(ClutchState::Engaged, getClutchState());
	EXPECT_TRUE(engine->engineState.clutchUpState);

	engine->engineState.lua.clutchUpState = false;
	engine->updateSwitchInputs();
	EXPECT_EQ(ClutchState::MiddleOrDisengaged, getClutchState());

	// A physical up switch takes over from Lua
	engineConfiguration->clutchUpPin = Gpio::G2;
	setMockState(Gpio::G2, true);
	engine->updateSwitchInputs();
	EXPECT_EQ(ClutchState::Engaged, getClutchState());

	// Resetting Lua drops it as a source
	engineConfiguration->clutchUpPin = Gpio::Unassigned;
	engine->resetLua();
	engine->updateSwitchInputs();
	EXPECT_EQ(ClutchState::Indeterminate, getClutchState());
}
