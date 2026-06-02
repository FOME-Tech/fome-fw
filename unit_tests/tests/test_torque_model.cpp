#include "pch.h"

#include "throttle_model.h"

namespace {
// Drive the real GearDetector into a single, known gear. With these numbers the gearbox ratio
// works out to exactly `gear1`, so the detector reports 1st gear and the total ratio at the
// engine is gear1 * finalDrive.
void detectGear(float gear1, float finalDrive) {
	engineConfiguration->driveWheelRevPerKm = 600;
	engineConfiguration->finalGearRatio = finalDrive;
	engineConfiguration->totalGearsCount = 1;
	engineConfiguration->gearRatio[0] = gear1;

	auto& gd = engine->module<GearDetector>().unmock();
	gd.onConfigurationChange(nullptr);

	// wheelRpm = 50 * 600 / 60 = 500; driveshaftRpm = 500 * finalDrive
	// gearboxRatio = engineRpm / driveshaftRpm = gear1  => engineRpm = gear1 * 500 * finalDrive
	Sensor::setMockValue(SensorType::VehicleSpeed, 50);
	Sensor::setMockValue(SensorType::Rpm, gear1 * 500 * finalDrive);
	gd.onSlowCallback();
}

// Force the gear detector into neutral (vehicle stopped -> no ratio available).
void detectNeutral() {
	engineConfiguration->driveWheelRevPerKm = 600;
	engineConfiguration->finalGearRatio = 4;
	engineConfiguration->totalGearsCount = 1;
	engineConfiguration->gearRatio[0] = 2;

	auto& gd = engine->module<GearDetector>().unmock();
	gd.onConfigurationChange(nullptr);

	Sensor::setMockValue(SensorType::VehicleSpeed, 0);
	Sensor::setMockValue(SensorType::Rpm, 800);
	gd.onSlowCallback();
}
} // namespace

// ============================ applyTorqueLimits ============================

TEST(TorqueModelLimits, NoLimitsWhenBothDisabled) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& tm = engine->module<TorqueModel>().unmock();

	// 0 means "disabled" for both limiters.
	engineConfiguration->torqueModel.engineMaximum = 0;
	engineConfiguration->torqueModel.axleMaximum = 0;
	detectGear(2, 4); // a gear is engaged, but axle limit is disabled

	EXPECT_FLOAT_EQ(tm.applyTorqueLimits(500), 500);
	EXPECT_FALSE(tm.limitedByEngineMax);
	EXPECT_FALSE(tm.limitedByAxleMax);
}

TEST(TorqueModelLimits, EngineMaxClampsAboveLimit) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& tm = engine->module<TorqueModel>().unmock();

	engineConfiguration->torqueModel.engineMaximum = 300;
	engineConfiguration->torqueModel.axleMaximum = 0;

	EXPECT_FLOAT_EQ(tm.applyTorqueLimits(500), 300);
	EXPECT_TRUE(tm.limitedByEngineMax);
	EXPECT_FALSE(tm.limitedByAxleMax);
}

TEST(TorqueModelLimits, EngineMaxPassesBelowLimit) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& tm = engine->module<TorqueModel>().unmock();

	engineConfiguration->torqueModel.engineMaximum = 300;
	engineConfiguration->torqueModel.axleMaximum = 0;

	EXPECT_FLOAT_EQ(tm.applyTorqueLimits(250), 250);
	EXPECT_FALSE(tm.limitedByEngineMax);
}

TEST(TorqueModelLimits, EngineMaxIsStrictlyGreaterThan) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& tm = engine->module<TorqueModel>().unmock();

	engineConfiguration->torqueModel.engineMaximum = 300;
	engineConfiguration->torqueModel.axleMaximum = 0;

	// Exactly at the limit is not "over" - no clamp, flag stays clear.
	EXPECT_FLOAT_EQ(tm.applyTorqueLimits(300), 300);
	EXPECT_FALSE(tm.limitedByEngineMax);
}

TEST(TorqueModelLimits, DoesNotLimitNegativeTorque) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& tm = engine->module<TorqueModel>().unmock();

	engineConfiguration->torqueModel.engineMaximum = 300;
	engineConfiguration->torqueModel.axleMaximum = 0;

	// Engine braking / overrun: negative request is well under any positive limit.
	EXPECT_FLOAT_EQ(tm.applyTorqueLimits(-150), -150);
	EXPECT_FALSE(tm.limitedByEngineMax);
}

TEST(TorqueModelLimits, AxleMaxClampsUsingGearRatio) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& tm = engine->module<TorqueModel>().unmock();

	engineConfiguration->torqueModel.engineMaximum = 0;
	engineConfiguration->torqueModel.axleMaximum = 1000;

	// 1000 Nm axle limit through an 8:1 total ratio -> 125 Nm at the engine.
	detectGear(/*gear1*/ 2, /*finalDrive*/ 4);

	EXPECT_FLOAT_EQ(tm.applyTorqueLimits(500), 125);
	EXPECT_TRUE(tm.limitedByAxleMax);
	EXPECT_FALSE(tm.limitedByEngineMax);

	// Below the engine-referred axle limit -> untouched.
	EXPECT_FLOAT_EQ(tm.applyTorqueLimits(100), 100);
	EXPECT_FALSE(tm.limitedByAxleMax);
}

TEST(TorqueModelLimits, AxleMaxScalesWithGearRatio) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& tm = engine->module<TorqueModel>().unmock();

	engineConfiguration->torqueModel.engineMaximum = 0;
	engineConfiguration->torqueModel.axleMaximum = 1000;

	// A taller gear (lower numeric ratio) multiplies torque less, so it permits more
	// engine torque for the same axle limit. 4:1 total -> 250 Nm at the engine.
	detectGear(/*gear1*/ 1, /*finalDrive*/ 4);

	EXPECT_FLOAT_EQ(tm.applyTorqueLimits(500), 250);
	EXPECT_TRUE(tm.limitedByAxleMax);
}

TEST(TorqueModelLimits, AxleMaxNotAppliedInNeutral) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& tm = engine->module<TorqueModel>().unmock();

	engineConfiguration->torqueModel.engineMaximum = 0;
	engineConfiguration->torqueModel.axleMaximum = 1000;

	// No gear engaged -> no ratio -> axle limit cannot be referred to the engine.
	detectNeutral();

	EXPECT_FLOAT_EQ(tm.applyTorqueLimits(500), 500);
	EXPECT_FALSE(tm.limitedByAxleMax);
}

TEST(TorqueModelLimits, AxleMaxDisabledWhenZeroEvenInGear) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& tm = engine->module<TorqueModel>().unmock();

	engineConfiguration->torqueModel.engineMaximum = 0;
	engineConfiguration->torqueModel.axleMaximum = 0;
	detectGear(2, 4);

	EXPECT_FLOAT_EQ(tm.applyTorqueLimits(500), 500);
	EXPECT_FALSE(tm.limitedByAxleMax);
}

TEST(TorqueModelLimits, MostRestrictiveLimitWins_AxleBinding) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& tm = engine->module<TorqueModel>().unmock();

	// Engine cap 300, axle-referred cap 125 (1000/8). Axle is the tighter one.
	engineConfiguration->torqueModel.engineMaximum = 300;
	engineConfiguration->torqueModel.axleMaximum = 1000;
	detectGear(/*gear1*/ 2, /*finalDrive*/ 4);

	EXPECT_FLOAT_EQ(tm.applyTorqueLimits(500), 125);
	EXPECT_TRUE(tm.limitedByEngineMax);
	EXPECT_TRUE(tm.limitedByAxleMax);
}

TEST(TorqueModelLimits, MostRestrictiveLimitWins_EngineBinding) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& tm = engine->module<TorqueModel>().unmock();

	// Engine cap 100, axle-referred cap 125 (1000/8). Engine is the tighter one.
	engineConfiguration->torqueModel.engineMaximum = 100;
	engineConfiguration->torqueModel.axleMaximum = 1000;
	detectGear(/*gear1*/ 2, /*finalDrive*/ 4);

	EXPECT_FLOAT_EQ(tm.applyTorqueLimits(500), 100);
	EXPECT_TRUE(tm.limitedByEngineMax);
	// The request also exceeded the axle-referred cap, so that flag is set too.
	EXPECT_TRUE(tm.limitedByAxleMax);
}

// ============================ onFastCallback flow ============================

namespace {
// A TorqueModel with every computation step stubbed out, so onFastCallback()'s wiring can be
// tested with no engine/airpath/sensor setup at all. Each leaf returns a canned value and the
// airmass sink captures whatever target it's handed.
class FlowMockTorqueModel : public TorqueModelBase {
public:
	float driverDemand() const override {
		return m_demand;
	}
	float getTorqueLoss() const override {
		return m_loss;
	}

	float applyTorqueLimits(float torqueRequested) override {
		m_limiterSawRequest = torqueRequested;
		return m_limited;
	}

	void commandAirmass(float airmassTarget) override {
		m_commandedAirmass = airmassTarget;
	}
	percent_t getThrottleRequest() override {
		return 0;
	}

	// Stubbed leaf outputs
	float m_demand = 0;
	float m_loss = 0;
	float m_limited = 0;

	// Captured inputs to the steps onFastCallback drives
	float m_limiterSawRequest = -1;
	float m_commandedAirmass = -1;
};
} // namespace

TEST(TorqueModelFlow, DisabledIsANoOp) {
	// onFastCallback only reads engineConfiguration->enableTorqueModel; EngineTestHelper
	// gives us a valid config without needing any torque-model setup.
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->enableTorqueModel = false;

	FlowMockTorqueModel tm;
	tm.m_demand = 250;
	tm.m_limited = 250;

	tm.onFastCallback();

	// Nothing ran: no demand collected, no limiter call, no airmass commanded.
	EXPECT_FLOAT_EQ(tm.m_driverTorqueDemand, 0);
	EXPECT_FLOAT_EQ(tm.m_limiterSawRequest, -1);
	EXPECT_FLOAT_EQ(tm.m_commandedAirmass, -1);
}

TEST(TorqueModelFlow, WiresDemandThroughLimiterToAirmass) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->enableTorqueModel = true;

	FlowMockTorqueModel tm;
	tm.m_demand = 250;	// driver asks for 250
	tm.m_limited = 250; // limiter passes it unchanged
	tm.m_loss = 0;

	tm.onFastCallback();

	// Driver demand becomes the requested torque, which is what reaches the limiter.
	EXPECT_FLOAT_EQ(tm.m_driverTorqueDemand, 250);
	EXPECT_FLOAT_EQ(tm.m_torqueRequested, 250);
	EXPECT_FLOAT_EQ(tm.m_limiterSawRequest, 250);

	EXPECT_FLOAT_EQ(tm.m_torqueRequestedLimited, 250);
	EXPECT_FLOAT_EQ(tm.m_torqueLoss, 0);
	EXPECT_FLOAT_EQ(tm.m_grossTorque, 250);

	// Airmass target is the 90 Nm/g hack on gross torque, and that exact value is commanded.
	EXPECT_FLOAT_EQ(tm.m_airmassTarget, 250.0f / 90);
	EXPECT_FLOAT_EQ(tm.m_commandedAirmass, 250.0f / 90);
}

TEST(TorqueModelFlow, UsesLimitedTorqueAndAddsLoss) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->enableTorqueModel = true;

	FlowMockTorqueModel tm;
	tm.m_demand = 500;	// driver asks for 500
	tm.m_limited = 300; // limiter clamps to 300
	tm.m_loss = 20;		// plus 20 Nm of loss

	tm.onFastCallback();

	// The full demand is what the limiter sees...
	EXPECT_FLOAT_EQ(tm.m_limiterSawRequest, 500);
	// ...but gross torque is built from the *limited* value, plus loss: 300 + 20 = 320.
	EXPECT_FLOAT_EQ(tm.m_torqueRequestedLimited, 300);
	EXPECT_FLOAT_EQ(tm.m_torqueLoss, 20);
	EXPECT_FLOAT_EQ(tm.m_grossTorque, 320);
	EXPECT_FLOAT_EQ(tm.m_airmassTarget, 320.0f / 90);
	EXPECT_FLOAT_EQ(tm.m_commandedAirmass, 320.0f / 90);
}
