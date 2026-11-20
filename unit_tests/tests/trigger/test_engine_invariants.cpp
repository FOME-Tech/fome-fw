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

extern WarningCodeState unitTestWarningCodeState;

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
		// 270 edges per crank revolution is 540 per cycle, which overflows
		// InstantRpmCalculator::timeOfLastEvent[PWM_PHASE_MAX_COUNT]
		trigger_type_e::TT_TRI_TACH,
		// The sync gap is exactly 1.5x, which is also exactly the lower edge of the
		// accepted range, so whether a real sync gap is accepted comes down to timestamp rounding
		trigger_type_e::TT_RENIX_44_2_2,
		trigger_type_e::TT_RENIX_66_2_2_2,
};

// On these low tooth count patterns, accelerating makes a tooth arrive earlier than
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

// Crank degrees between consecutive pulses of each of the first four injectors, from fromUs on, all within
// minDeg to maxDeg
static ::testing::AssertionResult
injectorSpacingWithin(const OutputRecorder& recorder, int64_t fromUs, double minDeg, double maxDeg) {
	std::ostringstream problems;
	int count = 0;

	for (int inj = 0; inj < 4; inj++) {
		std::vector<OutputPulse> pulses;
		for (const auto& p : recorder.pulses(OutputKind::Injector, inj)) {
			if (p.startUs >= fromUs) {
				pulses.push_back(p);
			}
		}

		if (pulses.size() < 4) {
			return ::testing::AssertionFailure() << "injector " << inj << " barely fired";
		}

		for (size_t i = 1; i < pulses.size(); i++) {
			double spacing = pulses[i].startAngle - pulses[i - 1].startAngle;
			if ((spacing < minDeg || spacing > maxDeg) && count++ < 5) {
				problems << "\n  injector " << inj << ": " << spacing << " degrees between pulses at "
						 << pulses[i - 1].startUs * 1e-3 << " ms";
			}
		}
	}

	if (count) {
		return ::testing::AssertionFailure()
			<< count << " injector pulse spacings outside " << minDeg << " to " << maxDeg << problems.str();
	}

	return ::testing::AssertionSuccess();
}

// The same for fuel, in batch mode, which needs no cam: each injector fires once per revolution, steady
// and through the acceleration, never skipped or doubled, and never left open
TEST_P(EngineInvariantsAllTriggers, BatchFuelSteadyThenAccelerate) {
	auto triggerType = (trigger_type_e)GetParam();

	if (skippedTriggers.count(triggerType)) {
		GTEST_SKIP() << "known problem, see skippedTriggers";
	}

	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	EXPECT_CALL(*eth.mockAirmass, getAirmass(::testing::_, ::testing::_))
			.WillRepeatedly(::testing::Return(AirmassResult{0.1008f, 50.0f}));

	engineConfiguration->isIgnitionEnabled = false;
	engineConfiguration->isInjectionEnabled = true;
	setCylinderCount(4);
	engineConfiguration->injectionMode = IM_BATCH;
	engineConfiguration->crankingInjectionMode = IM_BATCH;
	setTable(config->injectionPhase, -180.0f);
	setArrayValues(config->cltFuelCorrBins, 1.0f);
	setArrayValues(engineConfiguration->injector.battLagCorr, 0.0f);
	engine->updateSlowSensors();

	// Not setTriggerType(): that insists the type is different to the test engine's default
	engineConfiguration->trigger.type = triggerType;
	eth.applyTriggerWaveform();

	if (engine->triggerCentral.triggerShape.shapeDefinitionError) {
		GTEST_SKIP() << "no usable trigger shape";
	}

	OutputRecorder recorder;
	EngineSim sim(eth, &recorder);

	// Wheels that repeat more than once per revolution can't fire anything without a cam
	if (sim.wheelPeriod() < 360) {
		GTEST_SKIP() << "can't fire without a cam";
	}

	sim.setRpm(1000);
	sim.runCycles(10);
	auto steady = sim.nowUs();
	sim.runCycles(10);

	EXPECT_TRUE(injectorSpacingWithin(recorder, steady, 360 - 2, 360 + 2));

	auto accelerating = sim.nowUs();
	sim.rampRpm(5000, 1);
	sim.runSeconds(1.5);

	// Each pulse is timed from the last tooth before it at the speed then, which on a wheel with only one
	// or two teeth can be most of a cycle earlier, so timing wanders while accelerating. A skipped or
	// doubled pulse is still unmistakable: twice the spacing, or hardly any.
	EXPECT_TRUE(injectorSpacingWithin(recorder, accelerating, 180, 540));
	// Pulses are 1.5 ms
	EXPECT_TRUE(noStuckInjector(recorder, 2, steady));
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
// Known bugs
// ---------------------------------------------------------------------------------------------

// A tooth arriving earlier than predicted (accelerating) can make the spark fire before its coil was
// charged, and the coil is then left charging for a whole cycle
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

// After a trigger error RPM reads zero, and the engine looks stopped, until instant RPM has a full
// window of teeth again
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

// A single noise pulse on the cam input is taken as the cam tooth and flips engine phase by 360 degrees
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

// A cam sync tooth close to a crank sync point flips engine phase when it moves across it while running
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

// Every VVT position the firmware publishes for one bank 0 cam, sampled at each crank edge
static std::vector<std::pair<int64_t, float>>& logVvtPositions(EngineSim& sim, uint8_t camIndex = 0) {
	static std::vector<std::pair<int64_t, float>> log;
	log.clear();

	sim.onCrankEdge = [camIndex](const SimSample& sample) {
		auto vvt = engine->triggerCentral.getVVTPosition(0, camIndex);
		if (vvt) {
			log.push_back({sample.timeUs, vvt.Value});
		}
	};

	return log;
}

static ::testing::AssertionResult
vvtPositionsWithin(const std::vector<std::pair<int64_t, float>>& log, float expected, float tolerance, int64_t fromUs) {
	int bad = 0;
	float worst = 0;
	int64_t worstUs = 0;
	size_t checked = 0;

	for (const auto& [timeUs, vvt] : log) {
		if (timeUs < fromUs) {
			continue;
		}

		checked++;
		float error = std::abs(vvt - expected);
		if (error > tolerance) {
			bad++;
		}
		if (error > worst) {
			worst = error;
			worstUs = timeUs;
		}
	}

	if (checked == 0) {
		return ::testing::AssertionFailure() << "no VVT position was published";
	}

	if (bad == 0) {
		return ::testing::AssertionSuccess() << "worst error " << worst;
	}

	return ::testing::AssertionFailure() << bad << " of " << checked << " samples more than " << tolerance
										 << " deg from " << expected << ", worst error " << worst << " deg at "
										 << worstUs * 1e-3 << " ms";
}

// When a cam event changes engine phase while already fully synced, the VVT position from that same
// event must be measured against the new phase, not the old one (which would put it 360 degrees out).
//
// The legitimate way to get there: the first thing the cam input sees is a noise pulse a revolution
// away from the real tooth, so the first phase decision is wrong. The next real tooth corrects it.
TEST(EngineInvariants, VvtPositionRightWhenCamCorrectsPhase) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureSparkOnly(IM_INDIVIDUAL_COILS);
	setCrankOperationMode();
	engineConfiguration->vvtMode[0] = VVT_SINGLE_TOOTH;
	engineConfiguration->camInputs[0] = Gpio::A10;
	setToothedWheel(eth, 36, 1);

	OutputRecorder recorder;
	EngineSim sim(eth, &recorder);

	// Crank only to begin with
	sim.setRpm(1500);
	sim.runCycles(5);
	ASSERT_TRUE(sim.samples().back().synced);
	ASSERT_FALSE(sim.samples().back().fullSync);

	// The real cam tooth sits half way between crank sync points, far from any phase decision boundary.
	// Put its first appearance at least 400 degrees away...
	double tooth = sim.syncAngle() + 180;
	while (tooth < sim.angle() + 400) {
		tooth += 360;
	}
	size_t cam = sim.addCam(0, {{(float)std::fmod(tooth, 720), camToothWidth}});
	// ...so that a noise pulse a revolution earlier is the first thing the cam input sees
	sim.injectCamGlitch(cam, tooth - 360 - sim.angle(), /*widthDegrees*/ 5);

	auto& log = logVvtPositions(sim);
	sim.runCycles(6);

	// The scenario: full sync on the wrong phase, then corrected
	ASSERT_TRUE(sim.samples().back().fullSync);
	float finalAdjustment = sim.samples().back().phaseAdjustment;
	bool wasWrong = false;
	for (const auto& s : sim.samples()) {
		wasWrong |= s.fullSync && s.phaseAdjustment != finalAdjustment;
	}
	ASSERT_TRUE(wasWrong) << "the noise pulse didn't give the wrong phase first";

	ASSERT_FALSE(log.empty());
	float settled = log.back().second;

	// Every VVT position published is the real one. The cam never moves.
	EXPECT_TRUE(vvtPositionsWithin(log, settled, 1, 0));
}

// VVT position is extrapolated from the last crank tooth using the average speed
// of the previous crank revolution. On a wheel with few teeth, accelerating shows up as the cam moving.
static void
vvtThroughRamp(engine_type_e engineType, float camOffset, float fromRpm, float toRpm, float seconds, float tolerance) {
	EngineTestHelper eth(engineType);
	configureSparkOnly(IM_INDIVIDUAL_COILS);

	OutputRecorder recorder;
	EngineSim sim(eth, &recorder);
	auto cam = sim.addCamFromShape(0);
	sim.setCamOffset(cam, camOffset);

	auto& log = logVvtPositions(sim);

	sim.setRpm(fromRpm);
	sim.runCycles(20);
	ASSERT_TRUE(sim.samples().back().fullSync);
	ASSERT_TRUE(noTriggerErrors(sim));
	ASSERT_FALSE(log.empty());
	float steady = log.back().second;

	auto from = sim.nowUs();
	sim.rampRpm(toRpm, seconds);
	sim.runSeconds(seconds * 1.2f);

	EXPECT_TRUE(noTriggerErrors(sim, from));
	EXPECT_TRUE(vvtPositionsWithin(log, steady, tolerance, from))
			<< "cam offset " << camOffset << ", " << fromRpm << " -> " << toRpm << " rpm in " << seconds << " s";
}

TEST(EngineInvariants, DISABLED_VvtPositionSteadyWhileAcceleratingLowToothCrank) {
	// Miata NB2: 4 crank teeth per revolution, 70 and 110 degrees apart. Every cam position between two
	// crank teeth, at 6000 rpm/s (a free rev) and 5000 rpm/s back down. A camOffset of about 100 is
	// where a real NB2 cam sits at rest (VVT position 0), about 5 degrees before a crank tooth.
	// Today: up to 12 degrees of phantom movement accelerating and 7 decelerating.
	for (float camOffset = 10; camOffset < 180; camOffset += 20) {
		vvtThroughRamp(engine_type_e::HELLEN_NB2, camOffset, 1500, 7500, 1, 1);
		vvtThroughRamp(engine_type_e::HELLEN_NB2, camOffset, 7000, 2000, 1, 1);
	}
	vvtThroughRamp(engine_type_e::HELLEN_NB2, 100, 1500, 7500, 1, 1);
	vvtThroughRamp(engine_type_e::HELLEN_NB2, 100, 7000, 2000, 1, 1);
}

// The same on a 36-1 crank, which uses instant RPM: passes today, error is under 0.25 degrees
TEST(EngineInvariants, VvtPositionSteadyWhileAccelerating36m1) {
	for (float camToothAfterSync : {5.0f, 185.0f}) {
		for (auto [fromRpm, toRpm] : {std::pair{1500.0f, 7500.0f}, {7000.0f, 1000.0f}}) {
			EngineTestHelper eth(engine_type_e::TEST_ENGINE);
			OutputRecorder recorder;
			EngineSim* simPtr;
			configureCrankWheelWithCam(eth, simPtr, recorder, camToothAfterSync);
			std::unique_ptr<EngineSim> sim(simPtr);
			auto& log = logVvtPositions(*sim);

			sim->setRpm(fromRpm);
			sim->runCycles(20);
			ASSERT_TRUE(sim->samples().back().fullSync);
			ASSERT_FALSE(log.empty());
			float steady = log.back().second;

			auto from = sim->nowUs();
			sim->rampRpm(toRpm, 1);
			sim->runSeconds(1.2);

			EXPECT_TRUE(vvtPositionsWithin(log, steady, 0.5f, from)) << fromRpm << " -> " << toRpm;
		}
	}
}

// The "VVT sync position too close to trigger sync" warning: the phase decision changes at every crank
// sync point, so a cam tooth within a few degrees either side of any of them is warned about. Whether
// it warns mustn't depend on which crank revolution happened to be the first one synced.
static bool syncPositionWarned(float camToothAfterSync) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	OutputRecorder recorder;
	EngineSim* simPtr;
	configureCrankWheelWithCam(eth, simPtr, recorder, camToothAfterSync);
	std::unique_ptr<EngineSim> sim(simPtr);

	sim->setRpm(1500);
	sim->runCycles(10);
	EXPECT_TRUE(sim->samples().back().fullSync) << camToothAfterSync;

	return unitTestWarningCodeState.recentWarnings.find(ObdCode::CUSTOM_VVT_SYNC_POSITION) != nullptr;
}

TEST(EngineInvariants, VvtSyncPositionWarningNearAnyBoundary) {
	// 36-1 crank: the boundaries are the crank sync points, every 360 degrees. The sim always starts
	// in the same place, so 3 and 363 (and -3 and 357) are the same engine with the crank first syncing
	// in the other revolution.
	for (float after : {3.0f, -3.0f, 363.0f, 357.0f}) {
		EXPECT_TRUE(syncPositionWarned(after)) << "cam tooth " << after << " deg after a crank sync point";
	}

	// Well away from both, no warning
	EXPECT_FALSE(syncPositionWarned(180));
	EXPECT_FALSE(syncPositionWarned(540));
}

TEST(EngineInvariants, VvtSyncPositionWarningNearAnyBoundarySymmetricalCrank) {
	// Miata NB2: the crank pattern repeats every 180 degrees, so there are four boundaries per cycle.
	// The cam's own definition puts its sync tooth on one of them.
	auto warned = [](float camOffset) {
		EngineTestHelper eth(engine_type_e::HELLEN_NB2);
		configureSparkOnly(IM_INDIVIDUAL_COILS);
		OutputRecorder recorder;
		EngineSim sim(eth, &recorder);
		sim.setCamOffset(sim.addCamFromShape(0), camOffset);

		sim.setRpm(1500);
		sim.runCycles(10);
		EXPECT_TRUE(sim.samples().back().fullSync) << camOffset;

		return unitTestWarningCodeState.recentWarnings.find(ObdCode::CUSTOM_VVT_SYNC_POSITION) != nullptr;
	};

	for (float camOffset : {3.0f, -3.0f, 183.0f, 177.0f, 363.0f, 537.0f}) {
		EXPECT_TRUE(warned(camOffset)) << "cam offset " << camOffset;
	}

	for (float camOffset : {30.0f, 90.0f, 150.0f, 270.0f}) {
		EXPECT_FALSE(warned(camOffset)) << "cam offset " << camOffset;
	}
}

// On a cam without a decoder, any noise pulse is taken as the tooth, and its VVT position published until
// the real tooth comes round again (up to 720 degrees). This is an exhaust cam that has nothing to do
// with engine phase, so phase is fine, but the VVT closed loop sees a jump.
TEST(EngineInvariants, DISABLED_CamGlitchDoesNotMoveVvtPosition) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	OutputRecorder recorder;
	EngineSim* simPtr;
	configureCrankWheelWithCam(eth, simPtr, recorder);
	std::unique_ptr<EngineSim> sim(simPtr);
	engineConfiguration->vvtMode[1] = VVT_SINGLE_TOOTH;
	engineConfiguration->camInputs[1] = Gpio::A11;
	size_t exhaust = sim->addCam(1, {{(float)sim->syncAngle() + 300, camToothWidth}});

	auto& log = logVvtPositions(*sim, 1);

	sim->setRpm(3000);
	sim->runCycles(10);
	ASSERT_TRUE(sim->samples().back().fullSync);
	ASSERT_FALSE(log.empty());
	float steady = log.back().second;

	// One noise pulse, 100 degrees after where the real tooth was
	sim->runDegrees(720 - std::fmod(sim->angle() - sim->syncAngle() - 300 + 7200, 720) + 100);
	auto from = sim->nowUs();
	sim->injectCamGlitch(exhaust, 1, 5);
	sim->runCycles(4);

	EXPECT_TRUE(vvtPositionsWithin(log, steady, 1, from));
}
