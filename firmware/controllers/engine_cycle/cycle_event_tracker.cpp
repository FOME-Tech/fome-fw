/**
 * @file cycle_event_tracker.cpp
 */

#include "pch.h"

#include "cycle_event_tracker.h"

static int32_t toRotationUnits(float degrees) {
	return static_cast<int32_t>(std::lround(degrees * rotationUnitsPerDegree));
}

expected<CycleEventInstance>
CycleEventTracker::poll(const OutputShape& shape, const EnginePhaseInfo& tooth, float period) {
	// The next anchor at or after this tooth, by engine phase
	float nextAnchor = wrapToPeriod(shape.anchorPhase - tooth.currentEngPhase.angle, period);

	// Which repetition of it, in whole periods from that one. With nothing dealt with yet, start from the
	// one just behind, in case it can still be served late.
	int periods = -1;

	if (m_hasResolved) {
		// The first anchor at least minSeparation after the last one dealt with, but never more than one
		// period behind this tooth: anything older is gone.
		float sinceResolved = rotationDegreesBetween(m_resolvedAnchor, tooth.rotation);
		float wanted = std::ceil((minSeparation * period - sinceResolved - nextAnchor) / period);
		// The upper limit only keeps a nonsense lead from overflowing
		periods = static_cast<int>(clampF(-1, wanted, 64));
	}

	float anchorFromNow = nextAnchor + periods * period;

	// An anchor that went by, too late to serve, while this tracker wasn't running wasn't missed because of
	// anything decided here: move past it rather than reporting it. That is any such anchor if nothing has
	// been dealt with yet, or one that passed since the last call if that call wasn't on the previous
	// tooth (the caller didn't call, or decoding was lost). An anchor that is behind for any other reason
	// (it moved, or the engine phase was relabeled) is reported, for the caller to count as a skip.
	if (anchorFromNow < 0 && anchorFromNow + shape.tailDeg < 0) {
		float sinceLastRun = rotationDegreesBetween(m_lastRun, tooth.rotation);
		bool missedTeeth = sinceLastRun > m_lastToNextTooth + 0.5f;
		bool passedSinceLastRun = anchorFromNow > -sinceLastRun;

		if (!m_hasRun || (missedTeeth && passedSinceLastRun)) {
			m_resolvedAnchor = tooth.rotation + toRotationUnits(anchorFromNow);
			m_hasResolved = true;
			anchorFromNow += period;
		}
	}

	bool afterLabelJump = m_hasRun && tooth.phaseLabelJumpCount != m_lastJumpCount;
	float toNextTooth = wrapToPeriod(tooth.nextEngPhase.angle - tooth.currentEngPhase.angle, period);

	m_lastRun = tooth.rotation;
	m_lastToNextTooth = toNextTooth;
	m_lastJumpCount = tooth.phaseLabelJumpCount;
	m_hasRun = true;

	float startFromNow = anchorFromNow - shape.leadDeg;

	if (startFromNow >= toNextTooth) {
		// Not due yet: a later tooth comes first
		return unexpected;
	}

	return CycleEventInstance{
			.anchorFromNow = anchorFromNow,
			.startFromNow = startFromNow,
			.servable = startFromNow >= 0 || anchorFromNow + shape.tailDeg >= 0,
			.afterLabelJump = afterLabelJump,
	};
}

void CycleEventTracker::resolve(const CycleEventInstance& instance, const EnginePhaseInfo& tooth) {
	m_resolvedAnchor = tooth.rotation + toRotationUnits(instance.anchorFromNow);
	m_hasResolved = true;
}
