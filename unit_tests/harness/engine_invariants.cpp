/**
 * @file engine_invariants.cpp
 */

#include "pch.h"

#include "harness/engine_invariants.h"

#include <cmath>
#include <sstream>

namespace {
// Collects violations, reporting the first few in full and the rest as a count
class Violations {
public:
	std::ostream& add() {
		m_count++;

		if (m_count <= maxReported) {
			m_text << "\n  ";
			return m_text;
		}

		m_discard.str("");
		return m_discard;
	}

	::testing::AssertionResult result(const char* what) const {
		if (m_count == 0) {
			return ::testing::AssertionSuccess();
		}

		auto failure = ::testing::AssertionFailure();
		failure << what << ": " << m_count << " violation(s)" << m_text.str();

		if (m_count > maxReported) {
			failure << "\n  ...and " << (m_count - maxReported) << " more";
		}

		return failure;
	}

private:
	static constexpr size_t maxReported = 5;

	size_t m_count = 0;
	std::stringstream m_text;
	std::stringstream m_discard;
};

std::ostream& operator<<(std::ostream& os, const OutputPulse& pulse) {
	os << describe(pulse.kind) << " " << (int)pulse.index << " at " << pulse.startUs * 1e-3 << " ms";

	if (!std::isnan(pulse.startAngle)) {
		os << " (" << pulse.startAngle << " deg)";
	}

	return os;
}

void checkDuration(
		Violations& violations,
		const OutputRecorder& recorder,
		OutputKind kind,
		float minMs,
		float maxMs,
		int64_t fromUs) {
	for (const auto& pulse : recorder.pulses(kind)) {
		if (pulse.startUs < fromUs) {
			continue;
		}

		float duration = pulse.durationMs();

		if (duration > maxMs) {
			violations.add() << pulse << (pulse.stillActive ? " still active after " : " active for ") << duration
							 << " ms, limit " << maxMs;
		} else if (duration < minMs && !pulse.stillActive) {
			violations.add() << pulse << " active for only " << duration << " ms, minimum " << minMs;
		}
	}
}

double wrapSymmetric(double value, double period) {
	value = std::fmod(value, period);

	if (value >= period / 2) {
		value -= period;
	} else if (value < -period / 2) {
		value += period;
	}

	return value;
}
} // namespace

::testing::AssertionResult noOverdwell(const OutputRecorder& recorder, float maxDwellMs, int64_t fromUs) {
	Violations violations;
	checkDuration(violations, recorder, OutputKind::Coil, 0, maxDwellMs, fromUs);
	checkDuration(violations, recorder, OutputKind::TrailingCoil, 0, maxDwellMs, fromUs);
	return violations.result("coil overdwell");
}

::testing::AssertionResult noStuckInjector(const OutputRecorder& recorder, float maxOpenMs, int64_t fromUs) {
	Violations violations;
	checkDuration(violations, recorder, OutputKind::Injector, 0, maxOpenMs, fromUs);
	checkDuration(violations, recorder, OutputKind::InjectorStage2, 0, maxOpenMs, fromUs);
	return violations.result("injector open too long");
}

::testing::AssertionResult
dwellWithin(const OutputRecorder& recorder, float minDwellMs, float maxDwellMs, int64_t fromUs) {
	Violations violations;
	checkDuration(violations, recorder, OutputKind::Coil, minDwellMs, maxDwellMs, fromUs);
	return violations.result("dwell out of range");
}

::testing::AssertionResult
injectionPulseWithin(const OutputRecorder& recorder, float minMs, float maxMs, int64_t fromUs) {
	Violations violations;
	checkDuration(violations, recorder, OutputKind::Injector, minMs, maxMs, fromUs);
	return violations.result("injection pulse out of range");
}

::testing::AssertionResult noSparkWithoutCharge(const OutputRecorder& recorder) {
	Violations violations;

	for (const auto& write : recorder.writes()) {
		if (write.kind == OutputKind::Coil && !write.state && !write.changed) {
			violations.add() << "coil " << (int)write.index << " fired at " << write.timeUs * 1e-3
							 << " ms without being charged";
		}
	}

	return violations.result("spark without charge");
}

::testing::AssertionResult allOutputsOff() {
	Violations violations;

	for (size_t i = 0; i < MAX_CYLINDER_COUNT; i++) {
		if (enginePins.coils[i].getLogicValue()) {
			violations.add() << "coil " << i << " is on";
		}
		if (enginePins.trailingCoils[i].getLogicValue()) {
			violations.add() << "trailing coil " << i << " is on";
		}
		if (enginePins.injectors[i].getLogicValue()) {
			violations.add() << "injector " << i << " is on";
		}
		if (enginePins.injectorsStage2[i].getLogicValue()) {
			violations.add() << "stage 2 injector " << i << " is on";
		}
	}

	return violations.result("outputs left on");
}

::testing::AssertionResult sparkTimingWithin(
		const OutputRecorder& recorder,
		const EngineSim& sim,
		float expectedAdvance,
		float toleranceDegrees,
		int64_t fromUs,
		double periodDegrees) {
	Violations violations;
	size_t checked = 0;

	if (periodDegrees == 0) {
		periodDegrees = sim.wheelPeriod();
	}

	for (const auto& pulse : recorder.pulses(OutputKind::Coil)) {
		if (pulse.stillActive || pulse.endUs < fromUs || pulse.index >= engine->engineState.cylinderCount) {
			continue;
		}

		// Where the spark happened relative to this cylinder's own TDC, as degrees before it
		double phase = sim.enginePhase(pulse.endAngle);
		double advance = engine->cylinders[pulse.index].getAngleOffset() - phase;
		double error = wrapSymmetric(advance - expectedAdvance, periodDegrees);

		checked++;

		if (std::abs(error) > toleranceDegrees) {
			violations.add() << "coil " << (int)pulse.index << " fired at " << pulse.endUs * 1e-3 << " ms, " << error
							 << " deg more advanced than the expected " << expectedAdvance << " BTDC";
		}
	}

	if (checked == 0) {
		violations.add() << "no sparks to check";
	}

	return violations.result("spark timing");
}

::testing::AssertionResult
evenPulseCount(const OutputRecorder& recorder, OutputKind kind, size_t minPulses, int64_t fromUs) {
	Violations violations;

	size_t cylinderCount = engine->engineState.cylinderCount;
	std::vector<size_t> counts(cylinderCount);

	for (const auto& pulse : recorder.pulses(kind)) {
		if (pulse.startUs >= fromUs && pulse.index < cylinderCount) {
			counts[pulse.index]++;
		}
	}

	size_t least = *std::min_element(counts.begin(), counts.end());
	size_t most = *std::max_element(counts.begin(), counts.end());

	if (least < minPulses || most - least > 1) {
		auto& os = violations.add();
		os << describe(kind) << " pulse counts per cylinder:";
		for (size_t count : counts) {
			os << " " << count;
		}
		os << " (expected at least " << minPulses << " each, equal to within one)";
	}

	return violations.result("uneven pulse count");
}

std::vector<float> activeTimePerWindow(
		const OutputRecorder& recorder, OutputKind kind, int index, int64_t fromUs, int64_t windowUs, size_t count) {
	std::vector<float> result(count);

	for (const auto& pulse : recorder.pulses(kind, index)) {
		for (size_t i = 0; i < count; i++) {
			int64_t windowStart = fromUs + i * windowUs;
			int64_t windowEnd = windowStart + windowUs;

			int64_t overlap = std::min(pulse.endUs, windowEnd) - std::max(pulse.startUs, windowStart);
			if (overlap > 0) {
				result[i] += overlap * 1e-3f;
			}
		}
	}

	return result;
}

::testing::AssertionResult injectedPerWindowWithin(
		const OutputRecorder& recorder, int64_t fromUs, int64_t windowUs, size_t count, float minMs, float maxMs) {
	Violations violations;

	for (size_t cyl = 0; cyl < engine->engineState.cylinderCount; cyl++) {
		auto windows = activeTimePerWindow(recorder, OutputKind::Injector, cyl, fromUs, windowUs, count);

		for (size_t i = 0; i < count; i++) {
			if (windows[i] < minMs || windows[i] > maxMs) {
				violations.add() << "injector " << cyl << " was open for " << windows[i] << " ms in window " << i
								 << " (starting " << (fromUs + i * windowUs) * 1e-3 << " ms), expected " << minMs
								 << " to " << maxMs;
			}
		}
	}

	return violations.result("injected amount");
}

::testing::AssertionResult noTriggerErrors(const EngineSim& sim, int64_t fromUs) {
	Violations violations;
	bool first = true;
	uint32_t last = 0;

	for (const auto& sample : sim.samples()) {
		if (sample.timeUs < fromUs) {
			continue;
		}

		if (!first && sample.triggerErrors != last) {
			violations.add() << "trigger error at " << sample.timeUs * 1e-3 << " ms, crank angle " << sample.angle
							 << ", " << sample.trueRpm << " rpm";
		}

		first = false;
		last = sample.triggerErrors;
	}

	return violations.result("trigger errors");
}

::testing::AssertionResult staysSynced(const EngineSim& sim, int64_t fromUs) {
	Violations violations;

	for (const auto& sample : sim.samples()) {
		if (sample.timeUs >= fromUs && !sample.synced) {
			violations.add() << "not synchronized at " << sample.timeUs * 1e-3 << " ms, crank angle " << sample.angle;
		}
	}

	return violations.result("lost trigger sync");
}

::testing::AssertionResult rpmNeverZero(const EngineSim& sim, int64_t fromUs) {
	Violations violations;
	bool seenRpm = false;

	for (const auto& sample : sim.samples()) {
		if (sample.timeUs < fromUs) {
			continue;
		}

		if (sample.reportedRpm > 0) {
			seenRpm = true;
		} else if (seenRpm) {
			violations.add() << "RPM read zero at " << sample.timeUs * 1e-3 << " ms while turning at " << sample.trueRpm
							 << " rpm";
		}
	}

	return violations.result("RPM dropped to zero");
}

::testing::AssertionResult rpmWithin(const EngineSim& sim, float toleranceFraction, int64_t fromUs) {
	Violations violations;

	for (const auto& sample : sim.samples()) {
		if (sample.timeUs < fromUs) {
			continue;
		}

		if (std::abs(sample.reportedRpm - sample.trueRpm) > toleranceFraction * sample.trueRpm) {
			violations.add() << "RPM read " << sample.reportedRpm << " at " << sample.timeUs * 1e-3
							 << " ms while turning at " << sample.trueRpm << " rpm";
		}
	}

	return violations.result("RPM inaccurate");
}

::testing::AssertionResult phaseStable(const EngineSim& sim, int64_t fromUs) {
	Violations violations;
	bool first = true;
	float lastAdjustment = 0;
	uint8_t lastResyncs = 0;

	for (const auto& sample : sim.samples()) {
		if (sample.timeUs < fromUs) {
			continue;
		}

		if (!first && (sample.phaseAdjustment != lastAdjustment || sample.camResyncs != lastResyncs)) {
			violations.add() << "engine phase adjustment changed from " << lastAdjustment << " to "
							 << sample.phaseAdjustment << " at " << sample.timeUs * 1e-3 << " ms";
		}

		first = false;
		lastAdjustment = sample.phaseAdjustment;
		lastResyncs = sample.camResyncs;
	}

	return violations.result("engine phase changed");
}
