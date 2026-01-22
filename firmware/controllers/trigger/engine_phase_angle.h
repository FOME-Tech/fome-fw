#pragma once

struct TriggerPhase { };
struct EnginePhase { };

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
};

bool isPhaseInRange(float test, float current, float next);

inline bool isPhaseInRange(EngPhase test, const EnginePhaseInfo& phaseInfo) {
	return isPhaseInRange(test.angle, phaseInfo.currentEngPhase.angle, phaseInfo.nextEngPhase.angle);
}

inline bool isPhaseInRange(TrgPhase test, const EnginePhaseInfo& phaseInfo) {
	return isPhaseInRange(test.angle, phaseInfo.currentTrgPhase.angle, phaseInfo.nextTrgPhase.angle);
}
