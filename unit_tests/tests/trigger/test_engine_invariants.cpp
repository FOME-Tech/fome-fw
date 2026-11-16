/**
 * @file test_engine_invariants.cpp
 *
 * Tests that drive the whole trigger -> scheduling -> output chain with a simulated engine
 * (see harness/engine_sim.h) and check invariants of the result (see harness/engine_invariants.h),
 * instead of asserting on individual scheduled events.
 *
 * Tests named DISABLED_ reproduce known firmware bugs. They are written to pass once the bug is fixed:
 * enable them as part of the fix. Run them today with --gtest_also_run_disabled_tests.
 */

#include "pch.h"

#include "harness/engine_invariants.h"

#include <set>

// Spark only, fixed timing, so results don't depend on the fuel or timing tables
static void configureSparkOnly(ignition_mode_e mode, float fixedTiming = 10) {
	engineConfiguration->isFasterEngineSpinUpEnabled = true;
	engineConfiguration->ignitionMode = mode;
	engineConfiguration->timingMode = TM_FIXED;
	engineConfiguration->fixedTiming = fixedTiming;
	engineConfiguration->isIgnitionEnabled = true;
	engineConfiguration->isInjectionEnabled = false;
}

static void setToothedWheel(EngineTestHelper& eth, int total, int skipped) {
	engineConfiguration->trigger.customTotalToothCount = total;
	engineConfiguration->trigger.customSkippedToothCount = skipped;
	eth.setTriggerType(trigger_type_e::TT_TOOTHED_WHEEL);
}

// Crank-speed 36-1 wheel plus a single tooth cam, running sequential.
// The cam tooth rises camToothAfterSync crank degrees after a crank sync point
static constexpr float camToothWidth = 20;

static size_t configureCrankWheelWithCam(
		EngineTestHelper& eth, EngineSim*& sim, OutputRecorder& recorder, float camToothAfterSync = 180) {
	configureSparkOnly(IM_INDIVIDUAL_COILS);
	setCrankOperationMode();
	engineConfiguration->vvtMode[0] = VVT_SINGLE_TOOTH;
	engineConfiguration->camInputs[0] = Gpio::A10;
	setToothedWheel(eth, 36, 1);

	sim = new EngineSim(eth, &recorder);

	return sim->addCam(0, {{(float)sim->syncAngle() + camToothAfterSync, camToothWidth}});
}

// ---------------------------------------------------------------------------------------------
// The harness itself
// ---------------------------------------------------------------------------------------------

TEST(EngineSim, SteadyRunningIsAccurate) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureSparkOnly(IM_INDIVIDUAL_COILS);
	setToothedWheel(eth, 36, 1);

	OutputRecorder recorder;
	EngineSim sim(eth, &recorder);

	sim.setRpm(2000);
	sim.runCycles(10);
	auto from = sim.nowUs();
	sim.runCycles(20);

	EXPECT_TRUE(noTriggerErrors(sim));
	EXPECT_TRUE(staysSynced(sim, from));
	EXPECT_TRUE(rpmWithin(sim, 0.01f, from));

	// Sparks happen where the firmware was told to put them, judged by the model's own crank angle
	EXPECT_TRUE(sparkTimingWithin(recorder, sim, 10, 0.2f, from));
	EXPECT_TRUE(dwellWithin(recorder, 2.9f, 3.1f, from));
	EXPECT_TRUE(evenPulseCount(recorder, OutputKind::Coil, 19, from));
	EXPECT_TRUE(noSparkWithoutCharge(recorder));
}

TEST(EngineSim, AngleFollowsSpeedProfile) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setToothedWheel(eth, 36, 1);

	EngineSim sim(eth);

	// 1200 rpm is 7200 degrees per second
	sim.setRpm(1200);
	sim.runSeconds(1);
	EXPECT_NEAR(7200, sim.angle(), 0.1);

	// A linear ramp covers the same angle as its average speed
	sim.rampRpm(2400, 1);
	sim.runSeconds(1);
	EXPECT_NEAR(7200 + 10800, sim.angle(), 0.5);
	EXPECT_NEAR(2400, sim.trueRpm(), 1);

	// Stopped means stopped
	sim.setRpm(0);
	sim.runSeconds(1);
	EXPECT_NEAR(7200 + 10800, sim.angle(), 0.5);
}

TEST(EngineSim, GlitchIsSeenByDecoder) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setToothedWheel(eth, 36, 1);

	EngineSim sim(eth);
	sim.setRpm(2000);
	sim.runCycles(10);
	ASSERT_TRUE(noTriggerErrors(sim));

	// An extra pulse between two teeth
	auto from = sim.nowUs();
	sim.injectCrankGlitch(/*afterDegrees*/ 105, /*widthDegrees*/ 2);
	sim.runCycles(2);

	EXPECT_FALSE(noTriggerErrors(sim, from));
}

TEST(EngineSim, MissingToothIsSeenByDecoder) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setToothedWheel(eth, 36, 1);

	EngineSim sim(eth);
	sim.setRpm(2000);
	sim.runCycles(10);

	// One whole tooth: its rising and falling edge
	auto from = sim.nowUs();
	sim.dropCrankEdges(2);
	sim.runCycles(2);

	EXPECT_FALSE(noTriggerErrors(sim, from));
}

TEST(EngineSim, CamGivesFullSync) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	OutputRecorder recorder;
	EngineSim* simPtr;
	configureCrankWheelWithCam(eth, simPtr, recorder);
	std::unique_ptr<EngineSim> sim(simPtr);

	EXPECT_EQ(360, sim->wheelPeriod());

	sim->setRpm(1500);
	sim->runCycles(10);
	auto from = sim->nowUs();
	sim->runCycles(10);

	EXPECT_TRUE(sim->samples().back().fullSync);
	EXPECT_TRUE(phaseStable(*sim, from));
	EXPECT_TRUE(sparkTimingWithin(recorder, *sim, 10, 0.2f, from));

	// Sequential: each coil fires once per cycle
	EXPECT_TRUE(evenPulseCount(recorder, OutputKind::Coil, 9, from));
}

// ---------------------------------------------------------------------------------------------
// Every trigger type: steady running, then a brisk acceleration
// ---------------------------------------------------------------------------------------------

class EngineInvariantsAllTriggers : public ::testing::TestWithParam<int> {};

INSTANTIATE_TEST_SUITE_P(
		Triggers,
		EngineInvariantsAllTriggers,
		// Skip index 0, the custom toothed wheel, which the other tests in this file use
		::testing::Range(1, (int)trigger_type_e::TT_UNUSED),
		[](const ::testing::TestParamInfo<int>& paramInfo) {
			return std::string(getTrigger_type_e((trigger_type_e)paramInfo.param));
		});

// Trigger definitions that can't run at all in this harness, each for a known reason
static const std::set<trigger_type_e> skippedTriggers = {
		// trigger_review N1: 270 edges per crank revolution is 540 per cycle, which overflows
		// InstantRpmCalculator::timeOfLastEvent[PWM_PHASE_MAX_COUNT]
		trigger_type_e::TT_TRI_TACH,
		// trigger_review N2: the sync gap is exactly 1.5x, which is also exactly the lower edge of the
		// accepted range, so whether a real sync gap is accepted comes down to timestamp rounding
		trigger_type_e::TT_RENIX_44_2_2,
		trigger_type_e::TT_RENIX_66_2_2_2,
};

// trigger_review C1: on these low tooth count patterns, accelerating makes a tooth arrive earlier than
// predicted by more than the dwell time, the spark fires before its coil is charged, and the coil is
// then left charging for a whole cycle.
static const std::set<trigger_type_e> coilHangsWhenAccelerating = {
		trigger_type_e::TT_DAIHATSU,
		trigger_type_e::TT_VVT_MAZDA_SKYACTIV,
		trigger_type_e::TT_3_1_CAM,
		trigger_type_e::TT_MAZDA_MIATA_VVT_TEST,
		trigger_type_e::TT_FORD_TFI_PIP,
};

TEST_P(EngineInvariantsAllTriggers, SteadyThenAccelerate) {
	auto triggerType = (trigger_type_e)GetParam();

	if (skippedTriggers.count(triggerType)) {
		GTEST_SKIP() << "known problem, see skippedTriggers";
	}

	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureSparkOnly(IM_WASTED_SPARK);
	// Not setTriggerType(): that insists the type is different to the test engine's default
	engineConfiguration->trigger.type = triggerType;
	eth.applyTriggerWaveform();

	if (engine->triggerCentral.triggerShape.shapeDefinitionError) {
		GTEST_SKIP() << "no usable trigger shape";
	}

	OutputRecorder recorder;
	EngineSim sim(eth, &recorder);

	// Steady: let it find sync, then nothing may go wrong at all
	sim.setRpm(1000);
	sim.runCycles(10);
	auto steady = sim.nowUs();
	sim.runCycles(10);

	EXPECT_TRUE(noTriggerErrors(sim));
	EXPECT_TRUE(staysSynced(sim, steady));
	EXPECT_TRUE(rpmWithin(sim, 0.02f, steady));
	EXPECT_TRUE(noOverdwell(recorder, 10, steady));

	// Wheels that repeat more than once per revolution can't fire anything without a cam
	bool canFireWithoutCam = sim.wheelPeriod() >= 360;
	if (canFireWithoutCam) {
		EXPECT_TRUE(evenPulseCount(recorder, OutputKind::Coil, 4, steady));
	}

	// 4000 rpm/s: a free rev, or first gear in something quick
	auto accelerating = sim.nowUs();
	sim.rampRpm(5000, 1);
	sim.runSeconds(1.5);

	EXPECT_TRUE(noTriggerErrors(sim, accelerating));
	EXPECT_TRUE(staysSynced(sim, accelerating));
	EXPECT_TRUE(rpmNeverZero(sim, accelerating));

	if (coilHangsWhenAccelerating.count(triggerType)) {
		EXPECT_FALSE(noOverdwell(recorder, 10, accelerating))
				<< "Coil hang no longer reproduces here: remove this trigger from coilHangsWhenAccelerating";
	} else {
		EXPECT_TRUE(noOverdwell(recorder, 10, accelerating));
	}
}

// ---------------------------------------------------------------------------------------------
// Fuel and spark together
// ---------------------------------------------------------------------------------------------

TEST(EngineInvariants, InjectionAndSparkThroughAccelerationAndStall) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	EXPECT_CALL(*eth.mockAirmass, getAirmass(::testing::_, ::testing::_))
			.WillRepeatedly(::testing::Return(AirmassResult{0.3f, 50.0f}));

	configureSparkOnly(IM_INDIVIDUAL_COILS);
	engineConfiguration->isInjectionEnabled = true;
	engineConfiguration->injectionMode = IM_SEQUENTIAL;
	engineConfiguration->crankingInjectionMode = IM_SIMULTANEOUS;
	setToothedWheel(eth, 36, 1);

	OutputRecorder recorder;
	EngineSim sim(eth, &recorder);

	sim.setRpm(2000);
	sim.runCycles(10);
	auto steady = sim.nowUs();
	sim.runCycles(20);

	// Constant airmass at constant speed: every pulse is the same, and every cylinder gets one per cycle
	auto steadyPulses = recorder.pulses(OutputKind::Injector);
	ASSERT_FALSE(steadyPulses.empty());
	float pulseMs = steadyPulses.back().durationMs();
	EXPECT_GT(pulseMs, 1);
	EXPECT_TRUE(injectionPulseWithin(recorder, 0.99f * pulseMs, 1.01f * pulseMs, steady));
	EXPECT_TRUE(evenPulseCount(recorder, OutputKind::Injector, 19, steady));
	EXPECT_TRUE(evenPulseCount(recorder, OutputKind::Coil, 19, steady));
	EXPECT_TRUE(sparkTimingWithin(recorder, sim, 10, 0.2f, steady));

	// Accelerate hard, with some speed ripple for good measure
	auto accelerating = sim.nowUs();
	sim.setSpeedRipple(0.03f, /*pulsesPerRev*/ 2);
	sim.rampRpm(6500, 1);
	sim.runSeconds(1.5);

	EXPECT_TRUE(noTriggerErrors(sim));
	EXPECT_TRUE(rpmNeverZero(sim, accelerating));
	// The trigger has plenty of teeth, so timing should hold up even while the speed is changing
	EXPECT_TRUE(sparkTimingWithin(recorder, sim, 10, 2, accelerating));
	EXPECT_TRUE(noOverdwell(recorder, 6, accelerating));
	EXPECT_TRUE(noStuckInjector(recorder, 1.5f * pulseMs, accelerating));

	// The engine stops dead, part way through whatever it was doing
	sim.setSpeedRipple(0, 0);
	sim.setRpm(0);
	sim.runSeconds(3);

	EXPECT_TRUE(allOutputsOff());
	EXPECT_TRUE(noOverdwell(recorder, 6, accelerating));
	EXPECT_TRUE(noStuckInjector(recorder, 1.5f * pulseMs, accelerating));
	EXPECT_EQ(0, Sensor::getOrZero(SensorType::Rpm));

	for (size_t i = 0; i < engine->engineState.cylinderCount; i++) {
		EXPECT_EQ(0, enginePins.injectors[i].getOverlappingCounter()) << "injector " << i;
	}
}

// ---------------------------------------------------------------------------------------------
// Known bugs. See trigger_review/ for the analysis of each.
// ---------------------------------------------------------------------------------------------

// trigger_review C1
TEST(EngineInvariants, DISABLED_CoilNotLeftChargingWhenToothArrivesEarly) {
	// Every alignment of dwell and spark relative to the teeth
	for (int offset = 0; offset < 60; offset += 6) {
		EngineTestHelper eth(engine_type_e::TEST_ENGINE);
		configureSparkOnly(IM_INDIVIDUAL_COILS);
		engineConfiguration->globalTriggerAngleOffset = offset;
		// 12 evenly spaced teeth per engine cycle
		setToothedWheel(eth, 12, 0);

		OutputRecorder recorder;
		EngineSim sim(eth, &recorder);

		sim.setRpm(600);
		sim.runCycles(10);

		// The engine fires and picks up speed
		auto from = sim.nowUs();
		sim.rampRpm(1200, 0.1f);
		sim.runCycles(6);

		EXPECT_TRUE(noOverdwell(recorder, 12, from)) << "globalTriggerAngleOffset " << offset;
	}
}

// trigger_review R1
TEST(EngineInvariants, DISABLED_RpmSurvivesTriggerError) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureSparkOnly(IM_WASTED_SPARK);
	setToothedWheel(eth, 36, 1);

	OutputRecorder recorder;
	EngineSim sim(eth, &recorder);

	sim.setRpm(2000);
	sim.runCycles(20);
	ASSERT_TRUE(noTriggerErrors(sim));
	float startedSecondsAgo = engine->rpmCalculator.getSecondsSinceEngineStart(getTimeNowNt());

	auto from = sim.nowUs();
	sim.injectCrankGlitch(/*afterDegrees*/ 105, /*widthDegrees*/ 2);
	sim.runCycles(4);

	// The glitch costs us sync for a moment...
	EXPECT_FALSE(noTriggerErrors(sim, from));
	// ...but the engine never stopped turning, so nothing should think it did
	EXPECT_TRUE(rpmNeverZero(sim, from));
	EXPECT_TRUE(rpmWithin(sim, 0.3f, from));
	EXPECT_GT(engine->rpmCalculator.getSecondsSinceEngineStart(getTimeNowNt()), startedSecondsAgo);
}

// trigger_review V1
TEST(EngineInvariants, DISABLED_SingleCamGlitchDoesNotFlipPhase) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	OutputRecorder recorder;
	EngineSim* simPtr;
	size_t cam = configureCrankWheelWithCam(eth, simPtr, recorder);
	std::unique_ptr<EngineSim> sim(simPtr);

	sim->setRpm(1500);
	sim->runCycles(10);
	ASSERT_TRUE(sim->samples().back().fullSync);

	// Stop half way between two real cam teeth, which is a revolution away from both of them
	sim->runDegrees(360 + camToothWidth / 2);

	// One spurious cam pulse
	auto from = sim->nowUs();
	sim->injectCamGlitch(cam, /*afterDegrees*/ 1, /*widthDegrees*/ 5);
	sim->runCycles(4);

	EXPECT_TRUE(phaseStable(*sim, from));
}

// trigger_review V4
TEST(EngineInvariants, DISABLED_CamToothNearCrankSyncPointKeepsPhase) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	OutputRecorder recorder;
	EngineSim* simPtr;
	// The cam tooth sits just after the crank sync point
	size_t cam = configureCrankWheelWithCam(eth, simPtr, recorder, /*camToothAfterSync*/ 3);
	std::unique_ptr<EngineSim> sim(simPtr);

	sim->setRpm(1500);
	sim->runCycles(10);
	ASSERT_TRUE(sim->samples().back().fullSync);

	// A little belt stretch, or a phaser moving: the tooth wanders to just before the sync point
	// and back again. The engine is in the same place the whole time.
	auto from = sim->nowUs();
	for (int i = 0; i < 6; i++) {
		sim->setCamOffset(cam, (i % 2) ? 0 : -6);
		sim->runCycles(2);
	}

	EXPECT_TRUE(phaseStable(*sim, from));
}
