/**
 * @file test_cycle_event_tracker.cpp
 *
 * CycleEventTracker on its own, driven by a synthetic engine: every instance of an event is dealt with
 * exactly once, whatever the tooth spacing, however far ahead of its anchor the output starts, and through
 * engine phase relabels, gaps in decoding and the tracker not being called.
 */

#include "pch.h"

#include "cycle_event_tracker.h"

namespace {

constexpr float period = 720;

// Teeth with a known true crank angle. The engine phase the firmware believes can be relabeled, and the
// rotation counter can be wrong after a gap, independently of the true angle.
struct SyntheticEngine {
	explicit SyntheticEngine(float toothGap, double startAngle = 0)
		: gap(toothGap)
		, angle(startAngle) {}

	EnginePhaseInfo tooth() const {
		EnginePhaseInfo info{};
		info.currentEngPhase.angle = wrap(angle + labelOffset);
		info.nextEngPhase.angle = wrap(angle + gap + labelOffset);
		info.rotation = counterBase + static_cast<uint32_t>(std::llround((angle + counterError) * 100));
		info.phaseLabelJumpCount = jumps;
		return info;
	}

	void advance() {
		angle += gap;
	}

	void relabel(double by) {
		labelOffset += by;
		jumps++;
	}

	static float wrap(double a) {
		a = std::fmod(a, period);
		return a < 0 ? a + period : a;
	}

	float gap;
	double angle;
	double labelOffset = 0;
	double counterError = 0;
	uint32_t counterBase = 0;
	uint16_t jumps = 0;
};

enum class Outcome {
	Fired,
	SkippedLate,
	SkippedJump
};

struct Resolved {
	// True crank angle of the instance's anchor, and where its output started if it fired
	double anchor;
	double startedAt;
	Outcome outcome;
};

// Deals with due instances the way the injection consumer does: an instance late only because of a phase
// relabel is skipped, one that can no longer start is skipped, and anything else starts now or on time
struct Consumer {
	void onTooth(const SyntheticEngine& sim, const OutputShape& shape) {
		auto tooth = sim.tooth();
		auto instance = tracker.poll(shape, tooth, period);
		if (!instance) {
			return;
		}

		const auto& i = instance.Value;
		Resolved r{sim.angle + i.anchorFromNow, 0, Outcome::Fired};

		if (i.afterLabelJump && i.startFromNow < 0) {
			r.outcome = Outcome::SkippedJump;
		} else if (!i.servable) {
			r.outcome = Outcome::SkippedLate;
		} else {
			r.startedAt = sim.angle + std::max(0.0f, i.startFromNow);
		}

		tracker.resolve(i, tooth);
		resolved.push_back(r);
	}

	size_t count(Outcome outcome) const {
		size_t n = 0;
		for (const auto& r : resolved) {
			n += r.outcome == outcome;
		}
		return n;
	}

	CycleEventTracker tracker;
	std::vector<Resolved> resolved;
};

// Spacing between consecutive resolved anchors, skipping the first `skip` of them
std::vector<double> anchorSpacings(const std::vector<Resolved>& resolved, size_t skip = 0) {
	std::vector<double> result;
	for (size_t i = skip + 1; i < resolved.size(); i++) {
		result.push_back(resolved[i].anchor - resolved[i - 1].anchor);
	}
	return result;
}

void runFor(SyntheticEngine& sim, Consumer& consumer, const OutputShape& shape, double degrees) {
	double until = sim.angle + degrees;
	while (sim.angle < until) {
		consumer.onTooth(sim, shape);
		sim.advance();
	}
}

std::string describe(float gap, const OutputShape& shape) {
	std::ostringstream s;
	s << "gap " << gap << " anchor " << shape.anchorPhase << " lead " << shape.leadDeg << " tail " << shape.tailDeg;
	return s.str();
}

} // namespace

// Steady running: one instance per period, each started exactly where it should, for every tooth spacing,
// anchor position, and lead up to well over two periods (the injector open nearly continuously)
TEST(CycleEventTracker, OneInstancePerPeriod) {
	for (float gap : {1.0f, 6.0f, 12.0f, 30.0f, 90.0f, 360.0f}) {
		for (float anchor : {0.0f, 7.5f, 180.0f, 359.0f, 540.0f, 719.5f}) {
			for (float lead : {0.0f, 100.0f, 450.0f, 719.0f, 720.0f, 721.0f, 792.0f, 1440.0f, 1800.0f}) {
				for (float tail : {0.0f, 200.0f}) {
					OutputShape shape{anchor, lead, tail};
					SyntheticEngine sim(gap);
					Consumer consumer;
					runFor(sim, consumer, shape, 10 * period);

					// At start-up, every instance whose output should already have started is taken late: with
					// a lead of several periods, that's several, one per tooth. After that everything is on
					// time.
					size_t startup = 2 + 2 * static_cast<size_t>(std::ceil(lead / period));
					ASSERT_GT(consumer.resolved.size(), startup + 3) << describe(gap, shape);

					for (double spacing : anchorSpacings(consumer.resolved, startup)) {
						EXPECT_NEAR(period, spacing, 0.02) << describe(gap, shape);
					}

					for (size_t i = startup; i < consumer.resolved.size(); i++) {
						const auto& r = consumer.resolved[i];
						ASSERT_EQ(Outcome::Fired, r.outcome) << describe(gap, shape);
						EXPECT_NEAR(r.anchor - lead, r.startedAt, 0.02) << describe(gap, shape);
					}
				}
			}
		}
	}
}

// The output growing or shrinking at the last moment, at every point in the cycle: the instance it belongs
// to never changes, so nothing is skipped or repeated. Growing makes it start late, and with an anchor
// still ahead, a late start is still served.
TEST(CycleEventTracker, LeadStepsNeverSkipOrRepeat) {
	const std::pair<float, float> steps[] = {{450, 630}, {630, 450}, {450, 108}, {108, 450}, {200, 900}, {900, 200}};

	for (auto [from, to] : steps) {
		for (int toothOffset = 0; toothOffset < 60; toothOffset++) {
			SyntheticEngine sim(12);
			Consumer consumer;
			OutputShape shape{540, from, 0};

			runFor(sim, consumer, shape, 3 * period + toothOffset * 12);
			shape.leadDeg = to;
			runFor(sim, consumer, shape, 4 * period);

			for (double spacing : anchorSpacings(consumer.resolved, 2)) {
				EXPECT_NEAR(period, spacing, 0.02) << from << " to " << to << " at tooth " << toothOffset;
			}

			for (const auto& r : consumer.resolved) {
				ASSERT_EQ(Outcome::Fired, r.outcome) << from << " to " << to << " at tooth " << toothOffset;
				EXPECT_LE(r.startedAt, r.anchor + 0.01) << "started after its end anchor";
			}
		}
	}
}

// The anchor itself moving, gradually through the cycle boundary, or in a step: consecutive instances move
// apart or together by the change but never come closer than the minimum separation, and none is lost
TEST(CycleEventTracker, AnchorMoving) {
	// Gradually, through the cycle boundary and back
	{
		SyntheticEngine sim(12);
		Consumer consumer;
		OutputShape shape{600, 300, 0};

		for (int i = 0; i < 4 * 60; i++) {
			runFor(sim, consumer, shape, 12);
			shape.anchorPhase = SyntheticEngine::wrap(shape.anchorPhase + 1);
		}
		for (int i = 0; i < 4 * 60; i++) {
			runFor(sim, consumer, shape, 12);
			shape.anchorPhase = SyntheticEngine::wrap(shape.anchorPhase - 1);
		}

		for (double spacing : anchorSpacings(consumer.resolved, 1)) {
			EXPECT_GT(spacing, 720 - 70);
			EXPECT_LT(spacing, 720 + 70);
		}
		EXPECT_EQ(consumer.count(Outcome::Fired), consumer.resolved.size());
	}

	// In steps of up to 300 degrees either way, at every point in the cycle
	for (float step : {-300.0f, -100.0f, 100.0f, 300.0f}) {
		for (int toothOffset = 0; toothOffset < 60; toothOffset++) {
			SyntheticEngine sim(12);
			Consumer consumer;
			OutputShape shape{540, 200, 0};

			runFor(sim, consumer, shape, 3 * period + toothOffset * 12);
			shape.anchorPhase = SyntheticEngine::wrap(shape.anchorPhase + step);
			runFor(sim, consumer, shape, 4 * period);

			// A step back can put the next anchor behind the engine: that instance is lost, but it's
			// reported (and so counted), never silently dropped
			for (double spacing : anchorSpacings(consumer.resolved, 1)) {
				EXPECT_GE(spacing, CycleEventTracker::minSeparation * period) << "step " << step;
				EXPECT_LE(spacing, period + std::abs(step) + 0.02) << "step " << step;
			}
			if (step > 0) {
				EXPECT_EQ(0u, consumer.count(Outcome::SkippedLate)) << "step " << step;
			}
		}
	}
}

// The engine phase relabeled at every point in the cycle: no instance is ever dealt with twice or lost, and
// instances that only became late because of the relabel are skipped, never started late
TEST(CycleEventTracker, PhaseRelabel) {
	for (double jump : {360.0, -360.0, 180.0, -180.0, 5.0, -5.0}) {
		for (float tail : {0.0f, 200.0f}) {
			for (int toothOffset = 0; toothOffset < 60; toothOffset++) {
				SyntheticEngine sim(12);
				Consumer consumer;
				OutputShape shape{540, 200, tail};

				runFor(sim, consumer, shape, 3 * period + toothOffset * 12);
				sim.relabel(jump);
				runFor(sim, consumer, shape, 4 * period);

				for (double spacing : anchorSpacings(consumer.resolved, 1)) {
					EXPECT_GE(spacing, CycleEventTracker::minSeparation * period - 0.02)
							<< "jump " << jump << " tail " << tail << " at tooth " << toothOffset;
					EXPECT_LE(spacing, period + std::abs(jump) + 0.02)
							<< "jump " << jump << " tail " << tail << " at tooth " << toothOffset;
				}

				for (const auto& r : consumer.resolved) {
					if (r.outcome == Outcome::Fired) {
						EXPECT_GE(r.startedAt, r.anchor - shape.leadDeg - 0.02) << "started early";
					}
				}
			}
		}
	}
}

// Batch fuel before the cam syncs: two events half a cycle apart fire the same injectors, so between them
// those injectors get a pulse every 360 degrees. When the cam syncs and relabels the phase by 360 degrees,
// the two events trade places. Whatever the timing mode (how much of the pulse is after the anchor) and
// wherever the relabel lands, the injectors keep getting exactly one pulse every 360 degrees.
TEST(CycleEventTracker, BatchPairThroughCamSync) {
	for (float k : {1.0f, 0.5f, 0.0f}) {
		for (int toothOffset = 0; toothOffset < 60; toothOffset++) {
			SyntheticEngine sim(12);
			Consumer a, b;
			const float pulse = 200;
			OutputShape shapeA{540, k * pulse, (1 - k) * pulse};
			OutputShape shapeB{180, k * pulse, (1 - k) * pulse};

			auto run = [&](double degrees) {
				double until = sim.angle + degrees;
				while (sim.angle < until) {
					a.onTooth(sim, shapeA);
					b.onTooth(sim, shapeB);
					sim.advance();
				}
			};

			run(3 * period + toothOffset * 12);
			sim.relabel(360);
			run(4 * period);

			std::vector<double> starts;
			for (const auto* c : {&a, &b}) {
				for (const auto& r : c->resolved) {
					if (r.outcome == Outcome::Fired) {
						starts.push_back(r.startedAt);
					}
				}
			}
			std::sort(starts.begin(), starts.end());

			for (size_t i = 2; i < starts.size(); i++) {
				EXPECT_NEAR(360, starts[i] - starts[i - 1], 0.02) << "k " << k << " relabel at tooth " << toothOffset;
			}
		}
	}
}

// With nothing dealt with yet (engine start, or after a reset), nothing is reported that can't be served:
// instances that went by before the tracker started are simply passed over
TEST(CycleEventTracker, StartsCleanly) {
	for (double startAngle = 0; startAngle < period; startAngle += 37) {
		for (float lead : {0.0f, 300.0f, 800.0f}) {
			for (float tail : {0.0f, 150.0f}) {
				for (float anchor : {0.0f, 270.0f, 540.0f}) {
					SyntheticEngine sim(12, startAngle);
					Consumer consumer;
					OutputShape shape{anchor, lead, tail};

					runFor(sim, consumer, shape, 3 * period);
					consumer.tracker.reset();
					runFor(sim, consumer, shape, 3 * period);

					EXPECT_EQ(0u, consumer.count(Outcome::SkippedLate))
							<< "start " << startAngle << " " << describe(12, shape);
				}
			}
		}
	}
}

// The caller not calling for a while (fuel schedule not ready, RPM unknown): no backlog of old instances
// afterwards and nothing counted as late, and instances go on one per period
TEST(CycleEventTracker, NotCalledForAWhile) {
	for (int teethMissed = 1; teethMissed <= 120; teethMissed += 7) {
		SyntheticEngine sim(12);
		Consumer consumer;
		OutputShape shape{540, 300, 0};

		runFor(sim, consumer, shape, 3 * period);
		for (int i = 0; i < teethMissed; i++) {
			sim.advance();
		}
		runFor(sim, consumer, shape, 4 * period);

		EXPECT_EQ(0u, consumer.count(Outcome::SkippedLate)) << teethMissed << " teeth missed";

		// One instance per period, except where instances went by while not called: then a whole number of
		// periods, no more than the window could have covered
		int mostPeriods = 1 + static_cast<int>(std::ceil(teethMissed * 12 / period));
		for (double spacing : anchorSpacings(consumer.resolved)) {
			double periods = spacing / period;
			EXPECT_NEAR(std::round(periods), periods, 0.001) << spacing << " with " << teethMissed << " teeth missed";
			EXPECT_GE(std::round(periods), 1) << teethMissed << " teeth missed";
			EXPECT_LE(std::round(periods), mostPeriods) << teethMissed << " teeth missed";
		}
	}
}

// Decoding lost for a while, with the rotation estimated across the gap and wrong by some amount. Within
// the tolerance (more than the separation margin short, less than it long) no instance is repeated, and
// none is lost other than one that went by during the gap.
TEST(CycleEventTracker, RotationEstimatedWrongAcrossAGap) {
	for (double error : {-390.0, -200.0, -100.0, 0.0, 100.0, 200.0, 320.0}) {
		for (int gapStart = 0; gapStart < 60; gapStart += 5) {
			SyntheticEngine sim(12);
			Consumer consumer;
			OutputShape shape{540, 300, 0};

			runFor(sim, consumer, shape, 3 * period + gapStart * 12);
			for (int i = 0; i < 20; i++) {
				sim.advance();
			}
			sim.counterError += error;
			runFor(sim, consumer, shape, 4 * period);

			for (double spacing : anchorSpacings(consumer.resolved)) {
				bool onePeriod = std::abs(spacing - period) < 0.02;
				bool twoPeriods = std::abs(spacing - 2 * period) < 0.02;
				EXPECT_TRUE(onePeriod || twoPeriods) << spacing << " with error " << error << " gap at " << gapStart;
			}
		}
	}
}

// The rotation counter wraps around: nothing changes
TEST(CycleEventTracker, CounterWraps) {
	SyntheticEngine sim(12);
	sim.counterBase = 0xFFFFFFFFu - 100000;
	Consumer consumer;
	OutputShape shape{540, 300, 0};

	runFor(sim, consumer, shape, 10 * period);

	ASSERT_GT(consumer.resolved.size(), 8u);
	for (double spacing : anchorSpacings(consumer.resolved)) {
		EXPECT_NEAR(period, spacing, 0.02);
	}
}
