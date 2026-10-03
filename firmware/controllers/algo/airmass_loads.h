#pragma once

#include "rusefi_types.h"
#include <rusefi/expected.h>

enum class AirmassConsumer : uint8_t {
	InjectionPhase,
	FuelTrim,
	IgnitionTrim,
	Stft,
	Staging,
	LambdaDeviation,
	LambdaMonitor,
	TrailingSpark,
	IgnitionIat,
	KnockRetard,
	KnockGain,
	HpfpTarget,
	Count
};

// Full precision control values; packed output channels are display only.
struct AirmassLoadSnapshot {
	float Native = 0;
	float Map = 0;
	float EffectiveMap = 0;
	float Tps = 0;
	float Pedal = 0;
	float Filling = 0;
	uint8_t ValidSources = 0;
	bool Valid = false;
	bool UsesEstimate = false;
	bool RequiredFallbackUsed = false;
	bool LambdaTargetRequired = true;
	int ConfigurationVersion = 0;
};

float getAirmassConsumerLoad(AirmassConsumer consumer, size_t index = 0);
// Capture coordinates and selectors once for a calculation's cylinder loops.
// Immutable reads have no locks or diagnostic side effects. Keep this on the
// stack: the calculation owner must still guard its final publication against
// stops, faults and configuration writes after capture.
class AirmassConsumerLoadContext {
public:
	AirmassConsumerLoadContext(AirmassConsumer first, AirmassConsumer second = AirmassConsumer::Count);
	float get(AirmassConsumer consumer, size_t index = 0) const;

private:
	float m_loads[AFR_EffectiveMAP + 1];
	float m_hpfpMap;
	load_override_e m_sources[2][MAX_CYLINDER_COUNT];
	AirmassConsumer m_first;
	AirmassConsumer m_second;
};
static_assert(sizeof(AirmassConsumerLoadContext) <= 64);
// External models have no physical-model publication owner. The Fast calculation
// explicitly delivers their cursors once, after updating the legacy load state.
void publishLegacyAirmassConsumerLoads();
void invalidateAirmassConsumerLoads();
float getAirmassSelectedLoad(load_override_e source, float legacyDefault);
expected<float> getEffectiveAirmassMap();
bool isAirmassLambdaTargetRequired();
bool hasAirmassLoadFallback();
void invalidateAirmassLoads(bool engineStopped = false);
void updateBlendedVeAnalyzeQualification(float rpm);

#if EFI_UNIT_TEST
void resetAirmassCursorPreparationCounts();
uint32_t getAirmassCursorPreparationCount(AirmassConsumer consumer);
uint32_t getAirmassConsumerReadCount(AirmassConsumer consumer);
#endif
