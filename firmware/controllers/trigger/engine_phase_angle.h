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
