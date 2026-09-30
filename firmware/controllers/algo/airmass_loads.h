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
float getAirmassSelectedLoad(load_override_e source, float legacyDefault);
expected<float> getEffectiveAirmassMap();
bool isAirmassLambdaTargetRequired();
bool hasAirmassLoadFallback();
void invalidateAirmassLoads(bool engineStopped = false);
void updateBlendedVeAnalyzeQualification(float rpm);
