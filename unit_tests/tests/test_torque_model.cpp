#include "pch.h"

#include "throttle_model.h"

using ::testing::_;
using ::testing::Return;

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

// ============================ generic limiter tables ============================

TEST(TorqueModelLimits, GenericLimiterClampsAndPublishesAxes) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& tm = engine->module<TorqueModel>().unmock();

	engineConfiguration->torqueModel.engineMaximum = 0;
	engineConfiguration->torqueModel.axleMaximum = 0;

	auto& lim = engineConfiguration->torqueLimiters[0];
	lim.enable = true;
	lim.applyAtAxle = false;
	lim.xAxis = GPPWM_Rpm;
	lim.yAxis = GPPWM_Clt;
	copyArray(lim.xBins, {1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000});
	copyArray(lim.yBins, {0, 20, 40, 60, 80, 100, 120, 140});

	// Flat 200 Nm ceiling across the whole table.
	for (int y = 0; y < TORQUE_LIMITER_SIZE; y++) {
		for (int x = 0; x < TORQUE_LIMITER_SIZE; x++) {
			lim.table[y][x] = 200;
		}
	}

	Sensor::setMockValue(SensorType::Rpm, 3000);
	Sensor::setMockValue(SensorType::Clt, 60);

	// Above the ceiling -> clamps, flag and ceiling get published.
	EXPECT_FLOAT_EQ(tm.applyTorqueLimits(500), 200);
	EXPECT_TRUE(tm.limitedByGenericLimiter1);
	EXPECT_EQ(tm.m_limiterTorque[0], 200);
	EXPECT_NEAR(tm.m_limiterXAxisValue[0], 3000, 0.5);
	EXPECT_NEAR(tm.m_limiterYAxisValue[0], 60, 0.5);

	// Below the ceiling -> untouched, flag clears.
	EXPECT_FLOAT_EQ(tm.applyTorqueLimits(150), 150);
	EXPECT_FALSE(tm.limitedByGenericLimiter1);
}

TEST(TorqueModelLimits, GenericLimiterDisabledHasNoEffect) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& tm = engine->module<TorqueModel>().unmock();

	engineConfiguration->torqueModel.engineMaximum = 0;
	engineConfiguration->torqueModel.axleMaximum = 0;

	auto& lim = engineConfiguration->torqueLimiters[0];
	lim.enable = false; // disabled, table values must be ignored
	lim.xAxis = GPPWM_Rpm;
	lim.yAxis = GPPWM_Clt;
	for (int y = 0; y < TORQUE_LIMITER_SIZE; y++) {
		for (int x = 0; x < TORQUE_LIMITER_SIZE; x++) {
			lim.table[y][x] = 100;
		}
	}

	Sensor::setMockValue(SensorType::Rpm, 3000);
	Sensor::setMockValue(SensorType::Clt, 60);

	EXPECT_FLOAT_EQ(tm.applyTorqueLimits(500), 500);
	EXPECT_FALSE(tm.limitedByGenericLimiter1);
	EXPECT_EQ(tm.m_limiterTorque[0], 0);
}

TEST(TorqueModelLimits, GenericLimiterAxleDomainScalesByGearRatio) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& tm = engine->module<TorqueModel>().unmock();

	engineConfiguration->torqueModel.engineMaximum = 0;
	engineConfiguration->torqueModel.axleMaximum = 0;

	auto& lim = engineConfiguration->torqueLimiters[0];
	lim.enable = true;
	lim.applyAtAxle = true;
	lim.xAxis = GPPWM_Rpm;
	lim.yAxis = GPPWM_Zero;
	copyArray(lim.xBins, {1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000});
	copyArray(lim.yBins, {0, 20, 40, 60, 80, 100, 120, 140});

	// 1000 Nm flat at the axle.
	for (int y = 0; y < TORQUE_LIMITER_SIZE; y++) {
		for (int x = 0; x < TORQUE_LIMITER_SIZE; x++) {
			lim.table[y][x] = 1000;
		}
	}

	// Engaged with an 8:1 total ratio. detectGear sets the RPM mock to match the chosen ratio,
	// so the lookup happens at whatever column that corresponds to (the table is flat anyway).
	detectGear(/*gear1*/ 2, /*finalDrive*/ 4);

	// 1000 Nm axle / 8:1 total -> 125 Nm engine ceiling.
	EXPECT_FLOAT_EQ(tm.applyTorqueLimits(500), 125);
	EXPECT_TRUE(tm.limitedByGenericLimiter1);
}

TEST(TorqueModelLimits, GenericLimiterAxleDomainSuppressedInNeutral) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& tm = engine->module<TorqueModel>().unmock();

	engineConfiguration->torqueModel.engineMaximum = 0;
	engineConfiguration->torqueModel.axleMaximum = 0;

	auto& lim = engineConfiguration->torqueLimiters[0];
	lim.enable = true;
	lim.applyAtAxle = true;
	lim.xAxis = GPPWM_Rpm;
	lim.yAxis = GPPWM_Zero;
	copyArray(lim.xBins, {1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000});
	copyArray(lim.yBins, {0, 20, 40, 60, 80, 100, 120, 140});

	for (int y = 0; y < TORQUE_LIMITER_SIZE; y++) {
		for (int x = 0; x < TORQUE_LIMITER_SIZE; x++) {
			lim.table[y][x] = 100;
		}
	}

	// No gear known -> axle-domain limiter cannot be referred to the engine.
	detectNeutral();

	EXPECT_FLOAT_EQ(tm.applyTorqueLimits(500), 500);
	EXPECT_FALSE(tm.limitedByGenericLimiter1);
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
	expected<float> idleDemand(float /*driverDemand*/) override {
		return m_idleDemand;
	}
	float getTorqueLoss() override {
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
	float m_idleDemand = 0;
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
	EXPECT_NEAR(tm.m_airmassTarget, 250.0f / 90, 0.001);
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
	EXPECT_NEAR(tm.m_airmassTarget, 320.0f / 90, 0.001);
	EXPECT_FLOAT_EQ(tm.m_commandedAirmass, 320.0f / 90);
}

TEST(TorqueModelFlow, ArbitratesMaxOfDriverAndIdle) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->enableTorqueModel = true;

	FlowMockTorqueModel tm;
	// Driver off the pedal (idle corner), idle controller asking for more torque to hold RPM.
	tm.m_demand = 5;
	tm.m_idleDemand = 30;
	tm.m_limited = 30;

	tm.onFastCallback();

	// Both demands are logged; the larger one wins and is what the limiter sees.
	EXPECT_FLOAT_EQ(tm.m_driverTorqueDemand, 5);
	EXPECT_FLOAT_EQ(tm.m_idleTorqueDemand, 30);
	EXPECT_FLOAT_EQ(tm.m_torqueRequested, 30);
	EXPECT_FLOAT_EQ(tm.m_limiterSawRequest, 30);

	// On throttle: driver demand dominates, idle drops out of the max().
	tm.m_demand = 400;
	tm.m_idleDemand = 30;
	tm.m_limited = 400;
	tm.onFastCallback();
	EXPECT_FLOAT_EQ(tm.m_torqueRequested, 400);
}

// ============================ idleDemand ============================

namespace {
// Installs a mock IdleTargetController reporting the given idle target RPM and phase.
void mockIdlePhase(MockIdleTargetController& mock, float targetRpm, IIdleController::Phase phase) {
	IIdleTargetController::Output out;
	out.target.ClosedLoopTarget = targetRpm;
	out.phase = phase;
	EXPECT_CALL(mock, getOutput(_)).WillRepeatedly(Return(out));
	engine->engineModules.get<IdleTargetController>().set(&mock);
}
} // namespace

TEST(TorqueModelIdle, ClosedLoopOnlyWhenIdling) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& tm = engine->module<TorqueModel>().unmock();

	// Pure proportional for an easy check: 0.1 Nm per RPM error, +-50 Nm authority.
	engineConfiguration->torqueModel.idlePid.pFactor = 0.1f;
	engineConfiguration->torqueModel.idlePid.iFactor = 0;
	engineConfiguration->torqueModel.idlePid.dFactor = 0;
	engineConfiguration->torqueModel.idlePid.minValue = -50;
	engineConfiguration->torqueModel.idlePid.maxValue = 50;

	MockIdleTargetController mockTarget;
	mockIdlePhase(mockTarget, 1000, IIdleController::Phase::Idling);

	// Driver demand well below idle's output throughout, so idle stays the governor.
	// Below target -> positive torque demand to bring RPM up: 0.1 * (1000 - 900) = 10.
	engine->triggerCentral.instantRpm.m_instantRpm = 900;
	EXPECT_FLOAT_EQ(10, tm.idleDemand(-100).value_or(-1234));

	// Above target -> negative demand: 0.1 * (1000 - 1100) = -10.
	engine->triggerCentral.instantRpm.m_instantRpm = 1100;
	EXPECT_FLOAT_EQ(-10, tm.idleDemand(-100).value_or(-1234));

	// Authority clamp: huge error saturates at minValue/maxValue.
	engine->triggerCentral.instantRpm.m_instantRpm = 0;
	EXPECT_FLOAT_EQ(50, tm.idleDemand(-100).value_or(-1234));
}

TEST(TorqueModelIdle, ZeroOffIdle) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& tm = engine->module<TorqueModel>().unmock();

	engineConfiguration->torqueModel.idlePid.pFactor = 0.1f;
	engineConfiguration->torqueModel.idlePid.minValue = -50;
	engineConfiguration->torqueModel.idlePid.maxValue = 50;

	MockIdleTargetController mockTarget;
	// Not idling: even though RPM is well below the idle target, idle must not request torque -
	// the driver demand owns the throttle off-idle.
	mockIdlePhase(mockTarget, 1000, IIdleController::Phase::Running);

	engine->triggerCentral.instantRpm.m_instantRpm = 900;

	EXPECT_EQ(tm.idleDemand(0), unexpected);
}

TEST(TorqueModelIdle, DriverLiftsOffAtIdleOutput) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& tm = engine->module<TorqueModel>().unmock();

	// Pure proportional: 0.1 Nm per RPM error.
	engineConfiguration->torqueModel.idlePid.pFactor = 0.1f;
	engineConfiguration->torqueModel.idlePid.iFactor = 0;
	engineConfiguration->torqueModel.idlePid.dFactor = 0;
	engineConfiguration->torqueModel.idlePid.minValue = -50;
	engineConfiguration->torqueModel.idlePid.maxValue = 50;

	// The phase machine reports Idling while the driver is below idle's output, Running once above -
	// i.e. the handoff is keyed off the torque comparison, not a pedal threshold.
	MockIdleTargetController mockTarget;
	IIdleTargetController::Output idling;
	idling.target.ClosedLoopTarget = 1000;
	idling.phase = IIdleController::Phase::Idling;
	IIdleTargetController::Output running;
	running.target.ClosedLoopTarget = 1000;
	running.phase = IIdleController::Phase::Running;
	EXPECT_CALL(mockTarget, getOutput(false)).WillRepeatedly(Return(idling));
	EXPECT_CALL(mockTarget, getOutput(true)).WillRepeatedly(Return(running));
	engine->engineModules.get<IdleTargetController>().set(&mockTarget);

	// 100 RPM below target -> idle is providing 0.1 * 100 = 10 Nm.
	engine->triggerCentral.instantRpm.m_instantRpm = 900;

	// Governor seeds at 0; driver far below -> idle governs and provides 10.
	EXPECT_FLOAT_EQ(10, tm.idleDemand(-100).value_or(-1234));

	// Driver still asking for less than idle's 10 Nm -> idle keeps governing (no premature liftoff).
	EXPECT_FLOAT_EQ(10, tm.idleDemand(5).value_or(-1234));

	// Driver now asking for more than idle's 10 Nm -> driver has lifted off, idle abstains.
	EXPECT_EQ(tm.idleDemand(20), unexpected);
}

// ============================ getTorqueLoss ============================

namespace {
void setupLossAxes() {
	copyArray(config->torqueLossRpmBins, {0, 1000, 2000, 3000, 4000, 5000});
	copyArray(config->torqueLossLoadBins, {0, 20, 40, 60, 80, 100});
}
} // namespace

TEST(TorqueModelLoss, InterpolatesAcrossRpm) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& tm = engine->module<TorqueModel>().unmock();
	setupLossAxes();

	engineConfiguration->torqueModel.torqueLossLoadAxis = GPPWM_Clt;

	// Loss = 10 Nm per 1000 RPM, flat across the load axis.
	for (int y = 0; y < TORQUE_LOSS_SIZE; y++) {
		for (int x = 0; x < TORQUE_LOSS_SIZE; x++) {
			config->torqueLossTable[y][x] = config->torqueLossRpmBins[x] * 0.01f;
		}
	}

	Sensor::setMockValue(SensorType::Clt, 60);

	Sensor::setMockValue(SensorType::Rpm, 2000);
	EXPECT_NEAR(tm.getTorqueLoss(), 20, 0.1);

	// Halfway between the 2000 and 3000 RPM columns.
	Sensor::setMockValue(SensorType::Rpm, 2500);
	EXPECT_NEAR(tm.getTorqueLoss(), 25, 0.1);
}

TEST(TorqueModelLoss, UsesSelectedLoadAxis) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& tm = engine->module<TorqueModel>().unmock();
	setupLossAxes();

	// Loss depends only on the Y (load) axis here.
	for (int y = 0; y < TORQUE_LOSS_SIZE; y++) {
		for (int x = 0; x < TORQUE_LOSS_SIZE; x++) {
			config->torqueLossTable[y][x] = config->torqueLossLoadBins[y];
		}
	}

	Sensor::setMockValue(SensorType::Rpm, 3000);
	Sensor::setMockValue(SensorType::Clt, 0);
	Sensor::setMockValue(SensorType::Iat, 80);

	// CLT selected (=0) -> bottom load row.
	engineConfiguration->torqueModel.torqueLossLoadAxis = GPPWM_Clt;
	EXPECT_NEAR(tm.getTorqueLoss(), 0, 0.1);
	// The resolved Y-axis value is logged for the TS table cursor.
	EXPECT_NEAR(tm.m_torqueLossLoadAxisValue, 0, 0.1);

	// Switching the Y axis to IAT (=80) picks the top load row instead.
	engineConfiguration->torqueModel.torqueLossLoadAxis = GPPWM_Iat;
	EXPECT_NEAR(tm.getTorqueLoss(), 80, 0.1);
	EXPECT_NEAR(tm.m_torqueLossLoadAxisValue, 80, 0.1);
}

TEST(TorqueModelLoss, SupportsNegativeLoss) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto& tm = engine->module<TorqueModel>().unmock();
	setupLossAxes();

	engineConfiguration->torqueModel.torqueLossLoadAxis = GPPWM_Clt;

	for (int y = 0; y < TORQUE_LOSS_SIZE; y++) {
		for (int x = 0; x < TORQUE_LOSS_SIZE; x++) {
			config->torqueLossTable[y][x] = -15;
		}
	}

	Sensor::setMockValue(SensorType::Clt, 60);
	Sensor::setMockValue(SensorType::Rpm, 2500);

	EXPECT_NEAR(tm.getTorqueLoss(), -15, 0.1);
}
