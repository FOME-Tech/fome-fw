#pragma once

#include <cmath>

struct TriggerPhase {};
struct EnginePhase {};

template <typename TSpace>
struct PhaseAngle {
	float angle = 0;
};

using TrgPhase = PhaseAngle<TriggerPhase>;
using EngPhase = PhaseAngle<EnginePhase>;

// phase - phase -> angle
template <typename T>
inline float operator-(const PhaseAngle<T>& lhs, const PhaseAngle<T>& rhs) {
	return lhs.angle - rhs.angle;
}

// phase + angle -> phase
template <typename T>
inline PhaseAngle<T> operator+(const PhaseAngle<T>& lhs, float rhs) {
	return {lhs.angle + rhs};
}

// compare phases of the same type
template <typename T>
inline bool operator==(const PhaseAngle<T>& lhs, const PhaseAngle<T>& rhs) {
	return lhs.angle == rhs.angle;
}

struct EnginePhaseInfo {
	// When was this information captured?
	efitick_t timestamp;

	// Where is the engine, in trigger space?
	TrgPhase currentTrgPhase;
	TrgPhase nextTrgPhase;

	// Where is the engine, in engine space?
	EngPhase currentEngPhase;
	EngPhase nextEngPhase;

	// Physical crank rotation at this tooth, see TriggerCentral::getRotationCounter()
	uint32_t rotation = 0;
	// How many times the engine phase has been relabeled so far, see TriggerCentral::phaseLabelJumpCount
	uint16_t phaseLabelJumpCount = 0;
};

// Units of the rotation counter per degree
constexpr float rotationUnitsPerDegree = 100;

// Degrees of crank rotation from one rotation counter value to a later one (negative if it's earlier).
// The counter wraps, so only nearby values can be compared, which is all that's ever needed.
inline float rotationDegreesBetween(uint32_t from, uint32_t to) {
	return static_cast<int32_t>(to - from) / rotationUnitsPerDegree;
}

// Wrap an angle in to [0, period). Cheap for an angle already within one period of that range, as almost
// every angle on the tooth path is; anything further out falls back to fmod.
inline float wrapToPeriod(float angle, float period) {
	if (angle < 0) {
		angle += period;
	} else if (angle >= period) {
		angle -= period;
	}

	if (angle < 0 || angle >= period) {
		angle = std::fmod(angle, period);
		if (angle < 0) {
			angle += period;
		}
	}

	return angle;
}

bool isPhaseInRange(float test, float current, float next);

inline bool isPhaseInRange(EngPhase test, const EnginePhaseInfo& phaseInfo) {
	return isPhaseInRange(test.angle, phaseInfo.currentEngPhase.angle, phaseInfo.nextEngPhase.angle);
}

inline bool isPhaseInRange(TrgPhase test, const EnginePhaseInfo& phaseInfo) {
	return isPhaseInRange(test.angle, phaseInfo.currentTrgPhase.angle, phaseInfo.nextTrgPhase.angle);
}
