/**
 * @file test_trigger_error_recovery.cpp
 *
 * What happens when trigger sync is lost to a decode error and then regained, while the engine
 * keeps turning: RPM, ignition and injection modes, cam sync, the Miata NB tooth counter, and which
 * trigger errors get reported.
 *
 * Tests named DISABLED_ reproduce known firmware bugs. They are written to pass once the bug is fixed:
 * enable them as part of the fix. Run them today with --gtest_also_run_disabled_tests.
 */

#include "pch.h"

#include "harness/engine_invariants.h"

#include <map>
#include <set>
#include <sstream>

namespace {

void configureSparkOnly(ignition_mode_e mode, float fixedTiming = 10) {
	engineConfiguration->isFasterEngineSpinUpEnabled = true;
	engineConfiguration->ignitionMode = mode;
	engineConfiguration->timingMode = TM_FIXED;
	engineConfiguration->fixedTiming = fixedTiming;
	engineConfiguration->isIgnitionEnabled = true;
	engineConfiguration->isInjectionEnabled = false;
}

void setToothedWheel(EngineTestHelper& eth, int total, int skipped) {
	engineConfiguration->trigger.customTotalToothCount = total;
	engineConfiguration->trigger.customSkippedToothCount = skipped;
	eth.setTriggerType(trigger_type_e::TT_TOOTHED_WHEEL);
}

double wrap720(double a) {
	a = std::fmod(a, 720);
	return a < 0 ? a + 720 : a;
}

// Where in the 720 degree cycle (by true crank angle) each coil fired, taken from its last spark
// before beforeUs. With full sync and individual coils this is each cylinder's compression TDC
// (less the advance): the reference that later sparks are judged against.
std::map<int, double> firingPhases(const OutputRecorder& recorder, int64_t beforeUs) {
	std::map<int, double> result;

	for (const auto& p : recorder.pulses(OutputKind::Coil)) {
		if (!p.stillActive && p.endUs < beforeUs) {
			result[p.index] = wrap720(p.endAngle);
		}
	}

	return result;
}

bool firedWithAnotherCoil(const std::vector<OutputPulse>& pulses, const OutputPulse& p) {
	for (const auto& other : pulses) {
		if (other.index != p.index && !other.stillActive && std::abs(other.endUs - p.endUs) < 20) {
			return true;
		}
	}
	return false;
}

// Sparks on the exhaust stroke that weren't part of a wasted spark pair. With individual coils on
// the wrong engine phase, the coil fires on its cylinder's exhaust stroke and not at all on
// compression: a misfire, and a spark during valve overlap.
::testing::AssertionResult noLoneExhaustStrokeSpark(
		const OutputRecorder& recorder, const std::map<int, double>& reference, int64_t fromUs, float tolerance = 20) {
	auto pulses = recorder.pulses(OutputKind::Coil);

	std::ostringstream failures;
	int count = 0;

	for (const auto& p : pulses) {
		if (p.stillActive || p.endUs < fromUs) {
			continue;
		}

		auto ref = reference.find(p.index);
		if (ref == reference.end()) {
			continue;
		}

		double fromCompression = wrap720(p.endAngle - ref->second);
		bool onExhaustStroke = std::abs(fromCompression - 360) < tolerance;
		if (!onExhaustStroke) {
			continue;
		}

		if (!firedWithAnotherCoil(pulses, p)) {
			if (count < 5) {
				failures << "\n  coil " << (int)p.index << " fired alone on the exhaust stroke at " << p.endUs * 1e-3
						 << " ms";
			}
			count++;
		}
	}

	if (count == 0) {
		return ::testing::AssertionSuccess();
	}

	return ::testing::AssertionFailure() << count << " lone exhaust stroke spark(s):" << failures.str();
}

// Sparks that are neither where that coil fired before (its cylinder's compression stroke), nor a
// wasted spark: 360 degrees away, together with another coil
int misplacedSparks(
		const OutputRecorder& recorder,
		const std::map<int, double>& reference,
		int64_t fromUs,
		int64_t toUs = INT64_MAX,
		double* lastOffset = nullptr,
		float tolerance = 20) {
	auto pulses = recorder.pulses(OutputKind::Coil);
	int count = 0;

	for (const auto& p : pulses) {
		if (p.stillActive || p.endUs < fromUs || p.endUs > toUs) {
			continue;
		}
		auto ref = reference.find(p.index);
		if (ref == reference.end()) {
			continue;
		}

		double off = wrap720(p.endAngle - ref->second + 360) - 360;
		bool onCompression = std::abs(off) < tolerance;
		bool wasted = std::abs(std::abs(off) - 360) < tolerance && firedWithAnotherCoil(pulses, p);

		if (!onCompression && !wasted) {
			count++;
			if (lastOffset) {
				*lastOffset = off;
			}
		}
	}

	return count;
}

// Crank-speed 36-1 wheel plus a single tooth cam, running sequential.
std::unique_ptr<EngineSim> crankWheelWithSingleToothCam(EngineTestHelper& eth, OutputRecorder& recorder) {
	configureSparkOnly(IM_INDIVIDUAL_COILS);
	setCrankOperationMode();
	engineConfiguration->vvtMode[0] = VVT_SINGLE_TOOTH;
	engineConfiguration->camInputs[0] = Gpio::A10;
	setToothedWheel(eth, 36, 1);

	auto sim = std::make_unique<EngineSim>(eth, &recorder);
	sim->addCam(0, {{(float)sim->syncAngle() + 180, 20}});
	return sim;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// RPM must not read zero after a trigger error
// ---------------------------------------------------------------------------------------------

// The instant RPM case is EngineInvariants.DISABLED_RpmSurvivesTriggerError (36-1).
// This is the same thing on a wheel with too few teeth for instant RPM, which takes the per-revolution
// RPM path instead. It passes: that path holds the last value until a full revolution has been timed,
// which is what the instant RPM path should do too.
TEST(TriggerErrorRecovery, RpmSurvivesTriggerErrorWithoutInstantRpm) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureSparkOnly(IM_WASTED_SPARK);
	setToothedWheel(eth, 12, 1);
	// Too few teeth for instant RPM (see shouldUseInstantRpm)
	ASSERT_LT(engine->triggerCentral.triggerShape.primaryTeethPerCycle, 24u);
	ASSERT_FALSE(engineConfiguration->alwaysInstantRpm);

	OutputRecorder recorder;
	EngineSim sim(eth, &recorder);

	sim.setRpm(2000);
	sim.runCycles(20);
	ASSERT_TRUE(noTriggerErrors(sim));
	float startedSecondsAgo = engine->rpmCalculator.getSecondsSinceEngineStart(getTimeNowNt());

	auto from = sim.nowUs();
	sim.injectCrankGlitch(/*afterDegrees*/ 100, /*widthDegrees*/ 5);
	sim.runCycles(4);

	EXPECT_FALSE(noTriggerErrors(sim, from));
	EXPECT_TRUE(rpmNeverZero(sim, from));
	EXPECT_TRUE(rpmWithin(sim, 0.3f, from));
	EXPECT_GT(engine->rpmCalculator.getSecondsSinceEngineStart(getTimeNowNt()), startedSecondsAgo);
}

// RPM reading zero after a trigger error, measured at the outputs: a single noise pulse should cost at
// most the spark that was due while the decoder was actually unsynchronized, not the extra ones dropped
// while RPM reads zero afterwards.
TEST(TriggerErrorRecovery, DISABLED_SparksLostToOneGlitch) {
	int alignmentsThatLostSparks = 0;

	// Every alignment of the sparks relative to the sync point
	for (int offset = 0; offset < 180; offset += 15) {
		EngineTestHelper eth(engine_type_e::TEST_ENGINE);
		configureSparkOnly(IM_WASTED_SPARK);
		// Crank speed so that wasted spark fires every cylinder's coil once a revolution
		setCrankOperationMode();
		engineConfiguration->globalTriggerAngleOffset = offset;
		setToothedWheel(eth, 36, 1);

		OutputRecorder recorder;
		EngineSim sim(eth, &recorder);

		sim.setRpm(2000);
		sim.runCycles(20);

		auto from = sim.nowUs();
		size_t before = recorder.pulses(OutputKind::Coil).size();
		sim.injectCrankGlitch(/*afterDegrees*/ 105, /*widthDegrees*/ 2);
		sim.runCycles(4);
		size_t after = recorder.pulses(OutputKind::Coil).size();

		ASSERT_FALSE(noTriggerErrors(sim, from));

		// 4 cylinders, wasted spark on a crank wheel: 4 coil pulses per revolution, 8 per cycle
		int expected = 4 * 8;
		int lost = expected - (int)(after - before);
		printf("offset %3d: %d of %d coil pulses lost to one glitch, rpm zero: %s\n",
			   offset,
			   lost,
			   expected,
			   rpmNeverZero(sim, from) ? "no" : "yes");
		EXPECT_LE(lost, 2) << "globalTriggerAngleOffset " << offset;
		alignmentsThatLostSparks += lost > 0;
	}

	// The decoder is only unsynchronized for one tooth and the gap (the glitch is found at the tooth
	// before the gap, and the gap is the next sync point): about 20 of every 180 degrees between sparks.
	// Only a dwell that starts in there should be lost. Today RPM also reads zero for the next 90
	// degrees, and 8 of the 12 alignments lose a spark.
	EXPECT_LE(alignmentsThatLostSparks, 2);
}

// ---------------------------------------------------------------------------------------------
// Ignition and injection mode must fall back as soon as full sync is lost
// ---------------------------------------------------------------------------------------------

// Run with the glitch in each of the two revolutions of the cycle: losing sync restarts the crank
// revolution counter, so in one of them the firmware's guess at engine phase comes back 360 degrees
// out until the next cam tooth.
TEST(TriggerErrorRecovery, IndividualCoilsFallBackToWastedOnSyncLoss) {
	for (int extraRevs = 0; extraRevs < 2; extraRevs++) {
		EngineTestHelper eth(engine_type_e::TEST_ENGINE);
		OutputRecorder recorder;
		auto sim = crankWheelWithSingleToothCam(eth, recorder);

		sim->setRpm(1500);
		sim->runCycles(10);
		ASSERT_TRUE(sim->samples().back().fullSync);

		// Put the glitch just after the cam tooth, so the cam is as far away as it can be
		sim->runDegrees(360 * extraRevs + 200);

		auto from = sim->nowUs();
		auto reference = firingPhases(recorder, from);
		ASSERT_EQ(4u, reference.size());

		sim->injectCrankGlitch(/*afterDegrees*/ 20, /*widthDegrees*/ 2);
		sim->runCycles(3);

		ASSERT_FALSE(noTriggerErrors(*sim, from));
		EXPECT_TRUE(sim->samples().back().fullSync);

		EXPECT_TRUE(noLoneExhaustStrokeSpark(recorder, reference, from)) << "extraRevs " << extraRevs;
	}
}

// The injection side of the same thing. Each event keeps the mode it latched at the end of its last
// pulse. Measures fuel per cylinder per cycle through the glitch.
TEST(TriggerErrorRecovery, SequentialFuelThroughSyncLoss) {
	for (int extraRevs = 0; extraRevs < 2; extraRevs++) {
		EngineTestHelper eth(engine_type_e::TEST_ENGINE);
		EXPECT_CALL(*eth.mockAirmass, getAirmass(::testing::_, ::testing::_))
				.WillRepeatedly(::testing::Return(AirmassResult{0.3f, 50.0f}));

		OutputRecorder recorder;
		auto sim = crankWheelWithSingleToothCam(eth, recorder);
		engineConfiguration->isInjectionEnabled = true;
		engineConfiguration->injectionMode = IM_SEQUENTIAL;
		engineConfiguration->crankingInjectionMode = IM_SIMULTANEOUS;

		sim->setRpm(1500);
		sim->runCycles(10);
		ASSERT_TRUE(sim->samples().back().fullSync);
		sim->runDegrees(360 * extraRevs + 200);

		auto steadyPulses = recorder.pulses(OutputKind::Injector);
		ASSERT_FALSE(steadyPulses.empty());
		float pulseMs = steadyPulses.back().durationMs();

		auto from = sim->nowUs();
		sim->injectCrankGlitch(/*afterDegrees*/ 20, /*widthDegrees*/ 2);
		sim->runCycles(4);
		ASSERT_FALSE(noTriggerErrors(*sim, from));

		// 1500 rpm: 80 ms per cycle. Windows start a little before the glitch.
		int64_t cycleUs = 80000;
		int64_t windowStart = from - cycleUs / 2;
		for (int inj = 0; inj < 4; inj++) {
			auto perCycle = activeTimePerWindow(recorder, OutputKind::Injector, inj, windowStart, cycleUs, 4);
			float total = 0;
			for (float ms : perCycle) {
				total += ms / pulseMs;
			}

			// Batch fallback moves half pulses between cycles, and each half pulse carries its own
			// dead time, so individual cycles don't come out at exactly one pulse. But over the four
			// cycles, no injector may lose (or gain) a whole pulse: that's a lean (rich) misfire.
			// Losing one half pulse is unavoidable: the decoder has no position for part of a revolution.
			EXPECT_NEAR(4, total, 0.6f) << "extraRevs " << extraRevs << " injector " << inj;
		}
	}
}

// ---------------------------------------------------------------------------------------------
// The cam decoder must keep decoding while the crank is resyncing
// ---------------------------------------------------------------------------------------------

TEST(TriggerErrorRecovery, CamDecoderKeepsSyncThroughCrankResync) {
	for (int extraRevs = 0; extraRevs < 2; extraRevs++) {
		EngineTestHelper eth(engine_type_e::TEST_ENGINE);
		configureSparkOnly(IM_INDIVIDUAL_COILS);
		setCrankOperationMode();
		engineConfiguration->vvtMode[0] = VVT_FORD_ST170;
		engineConfiguration->camInputs[0] = Gpio::A10;
		setToothedWheel(eth, 36, 1);

		OutputRecorder recorder;
		EngineSim sim(eth, &recorder);
		sim.addCamFromShape(0);

		sim.setRpm(1500);
		sim.runCycles(10);
		ASSERT_TRUE(sim.samples().back().fullSync);
		auto& camDecoder = engine->triggerCentral.vvtState[0][0];
		ASSERT_TRUE(camDecoder.getShaftSynchronized());
		ASSERT_EQ(0u, camDecoder.triggerErrorCounter);

		sim.runDegrees(360 * extraRevs);

		// Lose one crank tooth: the decoder finds out at the next sync point, then needs another
		// revolution to resync. The cam keeps turning throughout.
		auto from = sim.nowUs();
		sim.dropCrankEdges(2);
		sim.runCycles(4);

		ASSERT_FALSE(noTriggerErrors(sim, from));

		// When the crank came back
		int64_t crankBackUs = -1;
		int64_t fullSyncBackUs = -1;
		bool lostCrank = false;
		bool lostFull = false;
		double crankBackAngle = 0;
		double fullBackAngle = 0;
		for (const auto& s : sim.samples()) {
			if (s.timeUs < from) {
				continue;
			}
			if (!s.synced) {
				lostCrank = true;
			} else if (lostCrank && crankBackUs < 0) {
				crankBackUs = s.timeUs;
				crankBackAngle = s.angle;
			}
			if (!s.fullSync) {
				lostFull = true;
			} else if (lostFull && fullSyncBackUs < 0) {
				fullSyncBackUs = s.timeUs;
				fullBackAngle = s.angle;
			}
		}

		ASSERT_GT(crankBackUs, 0);
		ASSERT_GT(fullSyncBackUs, 0);
		// The cam never missed a tooth, so it has no reason to report an error...
		EXPECT_EQ(0u, camDecoder.triggerErrorCounter) << "extraRevs " << extraRevs;
		// ...and should give full sync at the first cam sync point after the crank is back
		EXPECT_LT(fullBackAngle - crankBackAngle, 720) << "extraRevs " << extraRevs;
	}
}

// ---------------------------------------------------------------------------------------------
// Miata NB: below 1000 rpm the crank decoder counts teeth instead of checking gaps
// ---------------------------------------------------------------------------------------------

namespace {

struct NbGlitchResult {
	bool lostSync = false;
	bool errorCounted = false;
	bool fullSyncWhileUnsynced = false;
	// Sparks after the glitch that were neither on that cylinder's compression stroke nor a wasted spark
	int sparksOffPhase = 0;
	// Same, but only in the last cycle of the run: is it still wrong?
	int sparksOffPhaseAtEnd = 0;
	double offsetAtEnd = 0;
	int camResyncs = 0;
};

// Miata NB2 crank and cam, one noise pulse glitchAfter degrees after a crank sync point.
//
// Below 1000 rpm the decoder counts teeth instead of checking gaps, but only while it needs to: cranking
// and the first miataNbToothCountingCyclesAfterStart (20) engine cycles after start. The default
// warmupCycles puts the glitch inside that window, so tooth counting is in use.
//
// The cam is moved 30 degrees from where addCamFromShape puts it, which would otherwise be right on a
// crank sync point, where a cam tooth can flip engine phase on its own.
NbGlitchResult
miataNbGlitch(float glitchAfter, float rpm = 800, int cyclesAfter = 4, float camOffset = 30, int warmupCycles = 10) {
	EngineTestHelper eth(engine_type_e::HELLEN_NB2);
	configureSparkOnly(IM_INDIVIDUAL_COILS);
	EXPECT_EQ(trigger_type_e::TT_MIATA_VVT, engineConfiguration->trigger.type);
	EXPECT_EQ(VVT_MIATA_NB, engineConfiguration->vvtMode[0]);

	OutputRecorder recorder;
	EngineSim sim(eth, &recorder);
	auto cam = sim.addCamFromShape(0);
	sim.setCamOffset(cam, camOffset);

	sim.setRpm(rpm);
	sim.runCycles(warmupCycles);
	EXPECT_TRUE(sim.samples().back().fullSync);
	EXPECT_TRUE(noTriggerErrors(sim));

	// Line up on a crank sync point, so that glitchAfter is measured from a known tooth
	double syncPhase = std::fmod(sim.syncAngle(), 180);
	double now = std::fmod(sim.angle(), 180);
	sim.runDegrees(std::fmod(syncPhase - now + 360, 180) + 0.01);

	auto from = sim.nowUs();
	auto reference = firingPhases(recorder, from);
	EXPECT_EQ(4u, reference.size());
	auto camResyncsBefore = sim.samples().back().camResyncs;

	sim.injectCrankGlitch(glitchAfter, /*widthDegrees*/ 1);
	sim.runCycles(cyclesAfter - 1);
	auto lastCycle = sim.nowUs();
	sim.runCycles(1);

	NbGlitchResult r;
	for (const auto& s : sim.samples()) {
		if (s.timeUs >= from && !s.synced) {
			r.lostSync = true;
			r.fullSyncWhileUnsynced |= s.fullSync;
		}
	}
	r.errorCounted = !noTriggerErrors(sim, from);
	r.camResyncs = sim.samples().back().camResyncs - camResyncsBefore;

	r.sparksOffPhase = misplacedSparks(recorder, reference, from);
	r.sparksOffPhaseAtEnd = misplacedSparks(recorder, reference, lastCycle, INT64_MAX, &r.offsetAtEnd);

	return r;
}

} // namespace

// A glitch the tooth counter does notice (gap ratio outside 0.4 - 2.0) used to drop sync without going
// through onTriggerError: no error counted, and full sync kept with a phase that no longer matched. The
// glitch is in the first cycles after start, where tooth counting is in use.
TEST(TriggerErrorRecovery, MiataNbLostSyncBelow1000RpmIsATriggerError) {
	// Just after a tooth: a tiny gap, far below 0.4 of the previous one
	auto r = miataNbGlitch(/*glitchAfter*/ 5);

	ASSERT_TRUE(r.lostSync);
	// Losing sync is a trigger error like any other: counted, and full sync dropped until the cam
	// confirms phase again
	EXPECT_TRUE(r.errorCounted);
	EXPECT_FALSE(r.fullSyncWhileUnsynced);

	// No spark may land on the wrong cylinder. (This also needs the cam decoder to have kept going while
	// the crank resynced: the next full sync comes from it.)
	EXPECT_EQ(0, r.sparksOffPhase);
}

// A glitch with a plausible gap ratio is counted as a real tooth by the tooth counter, and the decoder
// used to carry on synchronized, one tooth off (timing 110 degrees out), for as long as the engine stayed
// below 1000 rpm. Now tooth counting is only used while the engine needs it, so once it has been running
// a while the gap check is used and catches the glitch as a trigger error.
TEST(TriggerErrorRecovery, MiataNbGlitchAtIdleDoesNotShiftTiming) {
	for (float glitchAfter = 5; glitchAfter < 180; glitchAfter += 10) {
		auto r = miataNbGlitch(glitchAfter, /*rpm*/ 800, /*cyclesAfter*/ 8, /*camOffset*/ 30, /*warmupCycles*/ 25);
		// Whatever the glitch did, a few cycles later timing must be right again
		EXPECT_EQ(0, r.sparksOffPhaseAtEnd) << "glitch " << glitchAfter << " deg after sync";
	}
}

// The same glitch is still taken as a tooth where tooth counting is still used: cranking and the first 20
// engine cycles after start. This glitch is in that window. Catching it would need something else, such
// as a cam cross-check that notices the crank position jumping by a tooth, which would cover this above
// cranking rpm but not cranking itself.
TEST(TriggerErrorRecovery, DISABLED_MiataNbGlitchJustAfterStartDoesNotShiftTiming) {
	for (float glitchAfter = 5; glitchAfter < 180; glitchAfter += 10) {
		auto r = miataNbGlitch(glitchAfter, /*rpm*/ 800, /*cyclesAfter*/ 8, /*camOffset*/ 30, /*warmupCycles*/ 10);
		EXPECT_EQ(0, r.sparksOffPhaseAtEnd) << "glitch " << glitchAfter << " deg after sync";
	}
}

// An idling engine that sags almost to a stall, lumpy, and recovers. This is decoded with the gap check
// rather than by counting teeth, and must not lose sync. (At 40% speed ripple it does, where tooth
// counting rode through: that's the price of only counting teeth after start, for an engine running
// that badly.)
TEST(TriggerErrorRecovery, MiataNbNearStallKeepsSync) {
	EngineTestHelper eth(engine_type_e::HELLEN_NB2);
	configureSparkOnly(IM_INDIVIDUAL_COILS);
	EngineSim sim(eth);
	auto cam = sim.addCamFromShape(0);
	sim.setCamOffset(cam, 30);

	sim.setRpm(850);
	sim.setSpeedRipple(0.03f, 2);
	sim.runCycles(30);
	ASSERT_TRUE(sim.samples().back().fullSync);
	ASSERT_GE(engine->rpmCalculator.getRevolutionCounterSinceStart(), 20u);

	auto from = sim.nowUs();
	sim.rampRpm(300, 0.5f);
	sim.setSpeedRipple(0.3f, 2);
	sim.runSeconds(1);
	sim.setSpeedRipple(0.1f, 2);
	sim.rampRpm(850, 0.5f);
	sim.runSeconds(1);

	EXPECT_TRUE(noTriggerErrors(sim, from));
	EXPECT_TRUE(staysSynced(sim, from));
	EXPECT_TRUE(sim.samples().back().fullSync);
}

// The cam decoder keeping going through a crank resync, on an engine that needs the cam before it may
// fire at all. The crank error goes through onTriggerError and drops full sync. If the cam decoder
// missed its edges while the crank resynced, its first sync point after that could be the wrong tooth:
// full sync would come back with the wrong phase, and sparks land on the wrong cylinders until the next
// good cam sync point.
TEST(TriggerErrorRecovery, MiataNbGlitchRecoversOnCorrectPhase) {
	for (float rpm : {800.0f, 1500.0f}) {
		for (float glitchAfter = 5; glitchAfter < 180; glitchAfter += 10) {
			auto r = miataNbGlitch(glitchAfter, rpm);
			if (!r.lostSync) {
				// In the first cycles after start, some glitch positions are accepted as a real tooth, see
				// DISABLED_MiataNbGlitchJustAfterStartDoesNotShiftTiming
				continue;
			}

			EXPECT_TRUE(r.errorCounted) << rpm << " rpm, glitch " << glitchAfter;
			EXPECT_FALSE(r.fullSyncWhileUnsynced) << rpm << " rpm, glitch " << glitchAfter;
			// The revolution with the glitch in it is decoded a tooth off until the decoder notices: up to
			// two sparks from that are unavoidable. With a cam decoder that missed edges, 5 to 10 landed on
			// the wrong cylinder.
			EXPECT_LE(r.sparksOffPhase, 2) << rpm << " rpm, glitch " << glitchAfter;
		}
	}
}

// RPM after a trigger error, under hard acceleration: 1000 to 7000 rpm in a second, glitch at several
// points on the way. RPM reads zero after every glitch. It also checks that a fix which holds the last
// RPM doesn't leave a coil charging while the speed changes (a stale RPM can make the spark fire before
// its coil was charged).
//
// Before ignition events were rebuilt after a trigger error, one case here (offset 60, glitch at 0.5 s)
// also left a coil charging for a whole revolution: the fast callback ran while RPM was 0 and computed a
// dwell angle of 0, and the overdwell backup fire latched that in to the event. The rebuild now replaces
// it before it is used.
TEST(TriggerErrorRecovery, DISABLED_GlitchDuringHardAccelerationKeepsRpmAndNoOverdwell) {
	for (int offset = 0; offset < 180; offset += 30) {
		for (float glitchAt : {0.1f, 0.3f, 0.5f, 0.7f}) {
			EngineTestHelper eth(engine_type_e::TEST_ENGINE);
			configureSparkOnly(IM_WASTED_SPARK);
			setCrankOperationMode();
			engineConfiguration->globalTriggerAngleOffset = offset;
			setToothedWheel(eth, 36, 1);

			OutputRecorder recorder;
			EngineSim sim(eth, &recorder);
			sim.setRpm(1000);
			sim.runCycles(10);

			auto from = sim.nowUs();
			sim.rampRpm(7000, 1);
			sim.runSeconds(glitchAt);
			sim.injectCrankGlitch(/*afterDegrees*/ 30, /*widthDegrees*/ 2);
			sim.runSeconds(1.2f - glitchAt);

			EXPECT_TRUE(noOverdwell(recorder, 6, from)) << "offset " << offset << " glitch at " << glitchAt << " s";
			EXPECT_TRUE(rpmNeverZero(sim, from)) << "offset " << offset << " glitch at " << glitchAt << " s";
		}
	}
}

// ---------------------------------------------------------------------------------------------
// The cam decoder runs before the crank is synchronized. Check that a cam that finds its own sync point
// first doesn't hurt anything.
// ---------------------------------------------------------------------------------------------

namespace {

struct CamPattern {
	const char* name;
	vvt_mode_e mode;
	// Miata NB2: symmetrical crank, must not fire at all before the cam has given phase
	bool miataNb;
};

const CamPattern camPatterns[] = {
		{"Miata NB2", VVT_MIATA_NB, true},
		{"36-1 + Ford ST170", VVT_FORD_ST170, false},
		{"36-1 + Bosch Quick Start", VVT_BOSCH_QUICK_START, false},
		{"36-1 + Barra 3+1", VVT_BARRA_3_PLUS_1, false},
		{"36-1 + Toyota 4-1", VVT_TOYOTA_4_1, false},
};

std::unique_ptr<EngineTestHelper> camPatternEngine(const CamPattern& pattern) {
	std::unique_ptr<EngineTestHelper> eth;

	if (pattern.miataNb) {
		eth = std::make_unique<EngineTestHelper>(engine_type_e::HELLEN_NB2);
		configureSparkOnly(IM_INDIVIDUAL_COILS);
	} else {
		eth = std::make_unique<EngineTestHelper>(engine_type_e::TEST_ENGINE);
		configureSparkOnly(IM_INDIVIDUAL_COILS);
		setCrankOperationMode();
		engineConfiguration->vvtMode[0] = pattern.mode;
		engineConfiguration->camInputs[0] = Gpio::A10;
		setToothedWheel(*eth, 36, 1);
	}

	EXPECT_EQ(pattern.mode, engineConfiguration->vvtMode[0]);
	EXPECT_TRUE(engine->triggerCentral.vvtTriggerConfiguration[0].needsTriggerDecoder()) << pattern.name;

	return eth;
}

} // namespace

namespace {

struct StartResult {
	bool skipped = false;
	bool camSyncedFirst = false;
	float vvt = NAN;
	// Where each coil fires at idle, true crank angle modulo the engine cycle
	std::map<int, double> firing;
};

// A start from rest: cranking, the engine catches, settles to idle. noise sends some garbage to the
// cam input before cranking starts.
StartResult startEngine(const CamPattern& pattern, float camOffset, double startAfterCrankSync, bool noise = false) {
	auto eth = camPatternEngine(pattern);

	OutputRecorder recorder;
	double crankSyncAngle;
	{
		EngineSim probe(*eth);
		crankSyncAngle = probe.syncAngle();
	}
	if (noise) {
		// Irregular noise on the cam input for a second and a half, crank not turning
		for (int i = 0; i < 40; i++) {
			eth->moveTimeForwardUs(5000 + 3000 * (i % 7));
			hwHandleVvtCamSignal(true, getTimeNowNt(), 0);
			eth->moveTimeForwardUs(200 + 100 * (i % 3));
			hwHandleVvtCamSignal(false, getTimeNowNt(), 0);
		}

		EXPECT_FALSE(engine->triggerCentral.triggerState.getShaftSynchronized());
		EXPECT_FALSE(engine->triggerCentral.triggerState.hasSynchronizedPhase());
		EXPECT_FALSE(engine->triggerCentral.getVVTPosition(0, 0).Valid);
		EXPECT_EQ(0, Sensor::getOrZero(SensorType::Rpm));
		EXPECT_TRUE(allOutputsOff());
	}

	EngineSim sim(*eth, &recorder, crankSyncAngle + startAfterCrankSync);
	auto cam = sim.addCamFromShape(0);
	sim.setCamOffset(cam, camOffset);

	const auto& camDecoder = engine->triggerCentral.vvtState[0][0];

	StartResult r;
	bool fullSyncWithoutCrank = false;
	bool vvtWithoutFullSync = false;
	sim.onCrankEdge = [&](const SimSample& s) {
		r.camSyncedFirst |= !s.synced && camDecoder.getShaftSynchronized();
		fullSyncWithoutCrank |= s.fullSync && !s.synced;
		vvtWithoutFullSync |= !s.fullSync && engine->triggerCentral.getVVTPosition(0, 0).Valid;
	};

	// Only a little ripple while cranking: enough to be realistic, not so much that the cam decoder
	// can't find its sync gap in the first revolution (which is the case this is for)
	sim.setRpm(250);
	sim.setSpeedRipple(0.03f, 2);
	sim.runSeconds(0.8);
	sim.rampRpm(1000, 0.6f);
	sim.runSeconds(0.6);
	sim.setSpeedRipple(0.03f, 2);
	sim.runSeconds(0.6);
	auto steady = sim.nowUs();
	sim.runSeconds(0.3);

	for (size_t i = 0; i < eth->recentWarnings()->getCount(); i++) {
		if (eth->recentWarnings()->get(i).Code == ObdCode::CUSTOM_VVT_SYNC_POSITION) {
			// Cam sync tooth right on a crank sync point, where it can flip engine phase on its own: not
			// what this is about
			r.skipped = true;
			return r;
		}
	}

	EXPECT_TRUE(sim.samples().back().synced);
	EXPECT_TRUE(sim.samples().back().fullSync);
	EXPECT_TRUE(noTriggerErrors(sim));
	// Several of these patterns log one cam error on every start, with or without the cam decoder
	// running ahead of the crank (the first, partial, cam cycle)
	EXPECT_LE(camDecoder.triggerErrorCounter, 1u);

	// The cam alone never gives engine phase, or a VVT position: only together with the crank
	EXPECT_FALSE(fullSyncWithoutCrank);
	EXPECT_FALSE(vvtWithoutFullSync);

	// Once engine phase was decided, it never changed
	int64_t fullSyncAt = -1;
	for (const auto& s : sim.samples()) {
		if (s.fullSync) {
			fullSyncAt = s.timeUs;
			break;
		}
	}
	EXPECT_GE(fullSyncAt, 0);
	EXPECT_TRUE(phaseStable(sim, fullSyncAt));

	auto vvt = engine->triggerCentral.getVVTPosition(0, 0);
	EXPECT_TRUE(vvt.Valid);
	r.vvt = vvt.value_or(NAN);
	for (const auto& p : recorder.pulses(OutputKind::Coil)) {
		if (!p.stillActive && p.endUs >= steady) {
			r.firing[p.index] = std::fmod(p.endAngle, 720);
		}
	}

	return r;
}

} // namespace

// Cam and crank finding their sync points in either order must make no difference at startup.
//
// In practice the cam almost never gets there first: the first crank tooth after standstill goes
// through the "engine stopped" timeout path, which resets the cam decoders, and the cam decoder then
// needs a few teeth of its own before it can recognise its sync gap. With a 36-1 crank, of all the cam
// patterns with a real decoder only the Ford ST170 at one cam position syncs first (checked once by
// sweeping them all). That case is pinned below; the sweep checks that nothing else changed.
//
// For each cam position the same start is repeated from a crank position a revolution later. Engine
// phase (where each coil fires) and the VVT reading must come out the same both times.
//
// Checks phase, not spark timing accuracy while cranking: with a sparse crank wheel and the engine
// catching, sparks can land late, and that's not what this test is about.
TEST(TriggerErrorRecovery, CamSyncBeforeCrankSyncStartsCleanly) {
	for (const auto& pattern : camPatterns) {
		int compared = 0;

		for (float camOffset = 0; camOffset < 720; camOffset += 60) {
			SCOPED_TRACE(::testing::Message() << pattern.name << ", cam moved " << camOffset);

			auto early = startEngine(pattern, camOffset, /*startAfterCrankSync*/ 15);
			auto late = startEngine(pattern, camOffset, /*startAfterCrankSync*/ 375);
			if (early.skipped || late.skipped) {
				continue;
			}

			compared++;
			EXPECT_NEAR(early.vvt, late.vvt, 2);
			EXPECT_EQ(early.firing.size(), late.firing.size());
			for (const auto& [coil, angle] : late.firing) {
				double diff = std::fmod(early.firing[coil] - angle + 720 + 360, 720) - 360;
				EXPECT_NEAR(0, diff, 3) << "coil " << coil;
			}
		}

		// Starts with the cam tooth right on a phase decision point are skipped. Miata NB2 has one every
		// 180 degrees, so 4 of the 12 positions here are skipped for it, and Bosch Quick Start loses 2.
		EXPECT_GE(compared, 8) << pattern.name;
	}

	// The one start where the cam really does sync before the crank
	const auto& st170 = camPatterns[1];
	ASSERT_EQ(VVT_FORD_ST170, st170.mode);
	auto camFirst = startEngine(st170, /*camOffset*/ 0, /*startAfterCrankSync*/ 15);
	auto crankFirst = startEngine(st170, /*camOffset*/ 0, /*startAfterCrankSync*/ 375);
	ASSERT_TRUE(camFirst.camSyncedFirst) << "the cam no longer syncs first here: find another case";
	EXPECT_FALSE(crankFirst.camSyncedFirst);
	EXPECT_NEAR(crankFirst.vvt, camFirst.vvt, 2);
	for (const auto& [coil, angle] : crankFirst.firing) {
		double diff = std::fmod(camFirst.firing[coil] - angle + 720 + 360, 720) - 360;
		EXPECT_NEAR(0, diff, 3) << "coil " << coil;
	}
}

// Where the cam really gets ahead of the crank: running, a crank tooth is lost, and while the crank
// decoder is resyncing (up to a revolution) the cam decoder keeps going and passes its own sync point.
// The tooth is lost at 24 positions round the cycle, so that this happens in some of them. Whatever the
// order, the cam must not set engine phase while the crank is unsynchronized, and once both are back,
// engine phase and VVT must be exactly what they were before.
TEST(TriggerErrorRecovery, CamSyncWhileCrankResyncsAfterError) {
	for (const auto& pattern : camPatterns) {
		int camSyncDuringCrankResync = 0;

		for (float lostAt = 0; lostAt < 720; lostAt += 30) {
			SCOPED_TRACE(::testing::Message() << pattern.name << ", crank tooth lost " << lostAt << " deg in");

			auto eth = camPatternEngine(pattern);
			OutputRecorder recorder;
			EngineSim sim(*eth, &recorder);
			auto cam = sim.addCamFromShape(0);
			sim.setCamOffset(cam, 30);
			const auto& camDecoder = engine->triggerCentral.vvtState[0][0];

			sim.setRpm(1500);
			sim.runCycles(10);
			ASSERT_TRUE(sim.samples().back().fullSync);

			auto vvtBefore = engine->triggerCentral.getVVTPosition(0, 0);
			ASSERT_TRUE(vvtBefore.Valid);
			auto before = firingPhases(recorder, INT64_MAX);
			ASSERT_EQ(4u, before.size());

			sim.runDegrees(lostAt);
			auto from = sim.nowUs();
			uint32_t camErrorsBefore = camDecoder.triggerErrorCounter;
			bool camSyncPointWhileCrankUnsynced = false;
			bool fullSyncWhileCrankUnsynced = false;
			auto lastCamCycle = camDecoder.getCrankSynchronizationCounter();
			sim.onCrankEdge = [&](const SimSample& s) {
				if (!s.synced && camDecoder.getCrankSynchronizationCounter() != lastCamCycle) {
					camSyncPointWhileCrankUnsynced = true;
				}
				lastCamCycle = camDecoder.getCrankSynchronizationCounter();
				fullSyncWhileCrankUnsynced |= s.fullSync && !s.synced;
			};

			sim.dropCrankEdges(2);
			sim.runCycles(4);
			auto after = sim.nowUs();
			sim.runCycles(1);

			ASSERT_FALSE(noTriggerErrors(sim, from)) << "losing the tooth didn't cost crank sync";
			camSyncDuringCrankResync += camSyncPointWhileCrankUnsynced;

			EXPECT_FALSE(fullSyncWhileCrankUnsynced);
			// The cam never missed an edge
			EXPECT_EQ(camErrorsBefore, camDecoder.triggerErrorCounter);

			ASSERT_TRUE(sim.samples().back().fullSync);
			auto vvtAfter = engine->triggerCentral.getVVTPosition(0, 0);
			ASSERT_TRUE(vvtAfter.Valid);
			EXPECT_NEAR(vvtBefore.Value, vvtAfter.Value, 1);

			std::map<int, double> recovered;
			for (const auto& p : recorder.pulses(OutputKind::Coil)) {
				if (!p.stillActive && p.endUs >= after) {
					recovered[p.index] = wrap720(p.endAngle);
				}
			}
			ASSERT_EQ(4u, recovered.size());
			for (const auto& [coil, angle] : before) {
				double diff = wrap720(recovered[coil] - angle + 360) - 360;
				EXPECT_NEAR(0, diff, 2) << "coil " << coil;
			}
		}

		// Make sure the case this test is about actually happened
		EXPECT_GE(camSyncDuringCrankResync, 2) << pattern.name;
	}
}

// Cam edges with the crank not turning at all: noise on the cam input with the key on, before
// cranking. The cam decoder may make what it likes of them, but nothing else may happen, and the
// start that follows must come out exactly as it would have without the noise.
TEST(TriggerErrorRecovery, CamNoiseWhileCrankStoppedIsHarmless) {
	for (const auto& pattern : camPatterns) {
		SCOPED_TRACE(pattern.name);

		auto clean = startEngine(pattern, /*camOffset*/ 60, /*startAfterCrankSync*/ 15);
		auto noisy = startEngine(pattern, /*camOffset*/ 60, /*startAfterCrankSync*/ 15, /*noise*/ true);
		ASSERT_FALSE(clean.skipped);
		ASSERT_FALSE(noisy.skipped);

		EXPECT_NEAR(clean.vvt, noisy.vvt, 2);
		EXPECT_EQ(clean.firing.size(), noisy.firing.size());
		for (const auto& [coil, angle] : clean.firing) {
			double diff = std::fmod(noisy.firing[coil] - angle + 720 + 360, 720) - 360;
			EXPECT_NEAR(0, diff, 3) << "coil " << coil;
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Expected trigger errors: a start shouldn't report any
// ---------------------------------------------------------------------------------------------

namespace {

// A normal start: cranking with the speed pulsing on every compression stroke, then the engine catches
void crankAndCatch(EngineSim& sim) {
	sim.setRpm(200);
	sim.setSpeedRipple(0.2f, 2);
	sim.runSeconds(1);
	sim.setSpeedRipple(0.05f, 2);
	sim.rampRpm(900, 0.5f);
	sim.runSeconds(1.5);
}

bool anyTriggerErrorWarning(EngineTestHelper& eth) {
	for (size_t i = 0; i < eth.recentWarnings()->getCount(); i++) {
		switch (eth.recentWarnings()->get(i).Code) {
			case ObdCode::CUSTOM_PRIMARY_TOO_MANY_TEETH:
			case ObdCode::CUSTOM_PRIMARY_NOT_ENOUGH_TEETH:
			case ObdCode::CUSTOM_CAM_TOO_MANY_TEETH:
			case ObdCode::CUSTOM_CAM_NOT_ENOUGH_TEETH:
				return true;
			default:
				break;
		}
	}
	return false;
}

} // namespace

// Several patterns lose sync once during a start: the first sync point is taken on trust, and the speed
// changes of cranking and catching make the next one fail. That's not a trigger problem, so it isn't
// reported (counted, warned) as one. Sync is still dropped and found again as always.
TEST(TriggerErrorRecovery, StartReportsNoTriggerErrorsOnAnyPattern) {
	static const std::set<trigger_type_e> skipped = {
			// see skippedTriggers in test_engine_invariants.cpp
			trigger_type_e::TT_TRI_TACH,
			trigger_type_e::TT_RENIX_44_2_2,
			trigger_type_e::TT_RENIX_66_2_2_2,
	};

	int patterns = 0;
	int lostSyncDuringStart = 0;

	for (int t = 1; t < (int)trigger_type_e::TT_UNUSED; t++) {
		auto type = (trigger_type_e)t;
		if (skipped.count(type)) {
			continue;
		}

		EngineTestHelper eth(engine_type_e::TEST_ENGINE);
		configureSparkOnly(IM_WASTED_SPARK);
		engineConfiguration->trigger.type = type;
		eth.applyTriggerWaveform();
		if (engine->triggerCentral.triggerShape.shapeDefinitionError) {
			continue;
		}
		SCOPED_TRACE(getTrigger_type_e(type));
		patterns++;

		EngineSim sim(eth);
		crankAndCatch(sim);

		bool synced = false;
		bool lostSync = false;
		for (const auto& s : sim.samples()) {
			lostSync |= synced && !s.synced;
			synced = s.synced;
		}
		lostSyncDuringStart += lostSync;

		EXPECT_TRUE(sim.samples().back().synced);
		EXPECT_TRUE(noTriggerErrors(sim));
		EXPECT_FALSE(anyTriggerErrorWarning(eth));
	}

	// Make sure the case this test is about actually happened
	EXPECT_GE(lostSyncDuringStart, 5);
}

// The same for the cam decoders: several patterns lose cam sync once during a start (the first, partial,
// cam cycle)
TEST(TriggerErrorRecovery, StartReportsNoCamErrors) {
	int lostCamSync = 0;

	for (const auto& pattern : camPatterns) {
		auto eth = camPatternEngine(pattern);
		SCOPED_TRACE(pattern.name);

		EngineSim sim(*eth);
		auto cam = sim.addCamFromShape(0);
		sim.setCamOffset(cam, 30);

		const auto& camDecoder = engine->triggerCentral.vvtState[0][0];
		bool camSynced = false;
		bool camLost = false;
		sim.onCrankEdge = [&](const SimSample&) {
			camLost |= camSynced && !camDecoder.getShaftSynchronized();
			camSynced = camDecoder.getShaftSynchronized();
		};

		crankAndCatch(sim);
		lostCamSync += camLost;

		EXPECT_TRUE(sim.samples().back().fullSync);
		EXPECT_EQ(0u, camDecoder.triggerErrorCounter);
		EXPECT_TRUE(noTriggerErrors(sim));
		EXPECT_FALSE(anyTriggerErrorWarning(*eth));
	}

	EXPECT_GE(lostCamSync, 2);
}

// Once sync has been confirmed, a real error while cranking is still reported: that's what someone
// chasing a hard start needs to see
TEST(TriggerErrorRecovery, GlitchWhileCrankingIsStillReported) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureSparkOnly(IM_WASTED_SPARK);
	setToothedWheel(eth, 36, 1);

	EngineSim sim(eth);
	sim.setRpm(200);
	sim.setSpeedRipple(0.1f, 2);
	sim.runCycles(3);
	ASSERT_TRUE(engine->triggerCentral.triggerState.isSyncConfirmed());
	ASSERT_TRUE(noTriggerErrors(sim));
	ASSERT_FALSE(engine->rpmCalculator.isRunning());

	auto from = sim.nowUs();
	sim.injectCrankGlitch(/*afterDegrees*/ 105, /*widthDegrees*/ 2);
	sim.runCycles(2);

	EXPECT_FALSE(noTriggerErrors(sim, from));
	EXPECT_TRUE(anyTriggerErrorWarning(eth));
}
