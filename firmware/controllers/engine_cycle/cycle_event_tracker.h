/**
 * @file cycle_event_tracker.h
 *
 * Decides, tooth by tooth, which repetition of a once-per-cycle event is next, and whether it has to be
 * started on this tooth.
 *
 * Each event (an injection now, maybe a spark later) has an anchor angle that repeats once per engine
 * cycle: one "instance" per repetition. Every instance must be dealt with exactly once, whether by
 * starting its output or by deliberately skipping it. The tracker remembers where the last dealt-with
 * instance was in physical crank rotation (TriggerCentral's rotation counter), not by engine phase,
 * because the engine phase can be relabeled when the cam syncs or corrects it. The next instance is the
 * first anchor that is far enough past the last one, so no instance can be dealt with twice, however far
 * ahead its output starts, and a start that moves can't make the tracker lose track of which instance
 * it belongs to.
 *
 * The tracker knows nothing about what the output is: the caller describes its shape around the anchor
 * and decides what to do with an instance that is due.
 */

#pragma once

#include "engine_phase_angle.h"

#include <rusefi/expected.h>

// Where an output sits around its anchor, for one instance
struct OutputShape {
	// The anchor, in engine phase [0, period). For injection, the end, middle or start of the pulse.
	float anchorPhase;
	// Degrees of output before the anchor, >= 0. May be more than a whole period.
	float leadDeg;
	// Degrees of output after the anchor, >= 0
	float tailDeg;
};

// An instance that has to be dealt with on this tooth
struct CycleEventInstance {
	// Degrees from this tooth to the instance's anchor. Negative if it has passed.
	float anchorFromNow;
	// Degrees from this tooth to where the output should start (anchorFromNow - leadDeg). Negative if
	// that has already passed: the output is late.
	float startFromNow;
	// Whether the output can still be started: either on time, or late but the end it was aiming for
	// (anchor plus tail) hasn't passed yet
	bool servable;
	// The engine phase was relabeled since this tracker last ran. An instance that is late only because
	// of that may belong to a cycle that has already been served under the old phase.
	bool afterLabelJump;
};

class CycleEventTracker {
public:
	// Instances must be at least this fraction of a period apart, in physical rotation
	static constexpr float minSeparation = 0.45f;

	// Call once per tooth. Returns the instance that has to be dealt with on this tooth, if any, which the
	// caller must then pass to resolve() whatever it decides to do with it.
	expected<CycleEventInstance> poll(const OutputShape& shape, const EnginePhaseInfo& tooth, float period);

	// The instance returned by poll() has been dealt with: its output started, or deliberately skipped
	void resolve(const CycleEventInstance& instance, const EnginePhaseInfo& tooth);

	// Forget everything: no instance has been dealt with. For an engine stop or a configuration change.
	void reset() {
		m_hasResolved = false;
		m_hasRun = false;
	}

private:
	// Rotation counter value at the anchor of the last instance dealt with
	uint32_t m_resolvedAnchor = 0;
	// Rotation counter value at the last poll(), and how far that tooth said the next one was
	uint32_t m_lastRun = 0;
	float m_lastToNextTooth = 0;
	// The engine phase relabel count at the last poll()
	uint16_t m_lastJumpCount = 0;

	bool m_hasResolved = false;
	bool m_hasRun = false;
};
