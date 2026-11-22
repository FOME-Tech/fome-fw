/**
 * @file test_rotation_counter.cpp
 *
 * TriggerCentral's rotation counter counts how far the crank has physically turned, from the spacing of
 * decoded teeth. Unlike the engine phase it must not jump when the phase is relabeled (the cam syncing or
 * correcting it), and it estimates its way across a gap in decoding.
 */

#include "pch.h"

#include "harness/engine_sim.h"

namespace {

void setToothedWheel(EngineTestHelper& eth, int total, int skipped) {
	engineConfiguration->trigger.customTotalToothCount = total;
	engineConfiguration->trigger.customSkippedToothCount = skipped;
	eth.setTriggerType(trigger_type_e::TT_TOOTHED_WHEEL);
}

// At every tooth the counter counted, how far it is from the true crank angle. Constant while counting is
// exact; anything else is an error in the count. The sim reports every edge, but only some are decoded
// teeth (others are falling edges the decoder ignores), so only edges that moved the counter are used.
struct CounterTracker {
	explicit CounterTracker(EngineSim& sim) {
		sim.onCrankEdge = [this](const SimSample& sample) {
			uint32_t rotation = getTriggerCentral()->getRotationCounter();
			bool moved = !haveLast || rotation != lastRotation;
			lastRotation = rotation;
			haveLast = true;

			if (!sample.synced || !moved) {
				return;
			}

			if (!started) {
				firstRotation = rotation;
				firstAngle = sample.angle;
				started = true;
			}

			double counted = rotationDegreesBetween(firstRotation, rotation);
			errors.push_back(counted - (sample.angle - firstAngle));
			times.push_back(sample.timeUs);
		};
	}

	// The spread of the counting error over samples taken at or after fromUs
	double spreadSince(int64_t fromUs) const {
		double lo = 1e9, hi = -1e9;
		for (size_t i = 0; i < errors.size(); i++) {
			if (times[i] >= fromUs) {
				lo = std::min(lo, errors[i]);
				hi = std::max(hi, errors[i]);
			}
		}
		return hi - lo;
	}

	// Counting error at the last sample, relative to the one at the first sample at or after fromUs
	double driftSince(int64_t fromUs) const {
		for (size_t i = 0; i < errors.size(); i++) {
			if (times[i] >= fromUs) {
				return errors.back() - errors[i];
			}
		}
		return 0;
	}

	bool haveLast = false;
	uint32_t lastRotation = 0;
	bool started = false;
	uint32_t firstRotation = 0;
	double firstAngle = 0;
	std::vector<double> errors;
	std::vector<int64_t> times;
};

} // namespace

// Through acceleration and deceleration, the counter follows the crank exactly, and with a crank wheel
// alone nothing ever relabels the engine phase
TEST(RotationCounter, FollowsCrankRotation) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setToothedWheel(eth, 60, 2);

	EngineSim sim(eth);
	CounterTracker tracker(sim);

	sim.setRpm(1000);
	sim.runCycles(10);
	auto from = sim.nowUs();
	sim.rampRpm(6000, 1);
	sim.runSeconds(1.5);
	sim.rampRpm(800, 1);
	sim.runSeconds(1.5);

	ASSERT_GT(tracker.errors.size(), 1000u);
	EXPECT_LT(tracker.spreadSince(from), 0.1);
	EXPECT_EQ(0, getTriggerCentral()->phaseLabelJumpCount);
}

// A crank wheel with a single tooth cam: until the cam is seen, which revolution is which is a guess. When
// the cam shows the guess was wrong, the engine phase is relabeled by 360 degrees, but the crank didn't
// move, so the counter mustn't either. Whether the first guess is wrong depends on which revolution the
// decoder starts on, so try the cam tooth on each.
TEST(RotationCounter, CamSyncRelabelsPhaseButNotRotation) {
	int relabeled = 0;

	for (float camRevolution : {0.0f, 360.0f}) {
		EngineTestHelper eth(engine_type_e::TEST_ENGINE);
		setCrankOperationMode();
		engineConfiguration->vvtMode[0] = VVT_SINGLE_TOOTH;
		engineConfiguration->camInputs[0] = Gpio::A10;
		setToothedWheel(eth, 36, 1);

		EngineSim sim(eth);
		CounterTracker tracker(sim);
		sim.addCam(0, {{(float)sim.syncAngle() + 180 + camRevolution, 20}});

		sim.setRpm(1500);
		sim.runCycles(10);
		ASSERT_TRUE(getTriggerCentral()->triggerState.hasSynchronizedPhase());

		// However the phase was labeled, the count never jumped
		EXPECT_LT(tracker.spreadSince(0), 0.1) << "cam on revolution " << camRevolution;

		// And once synced, it stays that way
		auto jumps = getTriggerCentral()->phaseLabelJumpCount;
		sim.runCycles(10);
		EXPECT_EQ(jumps, getTriggerCentral()->phaseLabelJumpCount) << "cam on revolution " << camRevolution;

		relabeled += jumps;
	}

	// One of the two must have started on the wrong revolution and been corrected
	EXPECT_GE(relabeled, 1);
}

// A noise pulse loses sync for up to a revolution. The teeth in between can't be counted, so the count
// is estimated from the time that passed. At a steady speed that estimate is close.
//
// The noise pulse itself is decoded as a tooth before the decoder notices anything is wrong, and so it
// counts for a whole tooth spacing (12 degrees here) wherever it actually fell. That much error is
// unavoidable, and far less than the half a cycle it would take to repeat or miss an event.
constexpr double toothSpacing = 12;
TEST(RotationCounter, EstimatesRotationAcrossLostSync) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setToothedWheel(eth, 60, 2);

	EngineSim sim(eth);
	CounterTracker tracker(sim);

	sim.setRpm(3000);
	sim.runCycles(10);
	auto from = sim.nowUs();
	auto errorsBefore = getTriggerCentral()->triggerState.triggerErrorCounter;

	sim.injectCrankGlitch(/*afterDegrees*/ 100, /*widthDegrees*/ 1);
	sim.runCycles(5);

	ASSERT_GT(getTriggerCentral()->triggerState.triggerErrorCounter, errorsBefore) << "sync wasn't lost";
	EXPECT_LT(std::abs(tracker.driftSince(from)), toothSpacing + 1);
}

// While speeding up through a gap, the estimate uses the slower speed, so it may count short but must
// not count long, beyond the noise pulse's own tooth: counting long is what could make an event repeat.
TEST(RotationCounter, LostSyncWhileAcceleratingNeverOvercounts) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setToothedWheel(eth, 60, 2);

	EngineSim sim(eth);
	CounterTracker tracker(sim);

	sim.setRpm(1500);
	sim.runCycles(10);
	auto from = sim.nowUs();
	auto errorsBefore = getTriggerCentral()->triggerState.triggerErrorCounter;

	sim.rampRpm(6000, 0.3f);
	sim.injectCrankGlitch(/*afterDegrees*/ 100, /*widthDegrees*/ 1);
	sim.runSeconds(0.3);

	ASSERT_GT(getTriggerCentral()->triggerState.triggerErrorCounter, errorsBefore) << "sync wasn't lost";
	EXPECT_LT(tracker.driftSince(from), toothSpacing + 0.5);
	EXPECT_GT(tracker.driftSince(from), -100);
}
