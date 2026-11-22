#pragma once

#include "sensor_converter_func.h"

// Low-pass filter on measured MAF flow, with a time constant of
// mafFilterParameter / RPM seconds. It runs as part of the MAF sensor's conversion
// chain, so it's called once per ADC update (ADC_UPDATE_RATE).
struct MafFilter final : public SensorConverter {
	SensorResult convert(float input) const override;

	mutable float m_lastValue = 0;
};
