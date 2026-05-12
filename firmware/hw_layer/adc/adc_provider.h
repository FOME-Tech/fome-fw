#pragma once

#include <cstddef>

struct AdcProvider {
	static float getVoltage(adc_channel_e channel);
	static bool acquire(adc_channel_e channel);
	static void release(adc_channel_e channel);

	// Get the name of this ADC provider
	virtual const char* name() const = 0;

	// Enable a channel. Returns true if successful, false if error.
	virtual bool enable(const char* name, size_t idx) = 0;
	virtual void disable(size_t idx) = 0;

	// Get the voltage on the idx-th channel of this provider
	virtual float get(size_t idx) const = 0;
};

void registerAdcProvider(AdcProvider&, size_t firstChannelIndex, size_t numChannels);
