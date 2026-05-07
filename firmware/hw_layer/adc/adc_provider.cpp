#include "pch.h"

#include "adc_provider.h"

namespace {
struct ProviderEntry {
	AdcProvider* provider;
	size_t firstChannelIndex;
	size_t numChannels;
};

constexpr size_t MAX_PROVIDERS = 4;

static ProviderEntry s_providers[MAX_PROVIDERS];
static size_t s_providerCount = 0;

// Returns the provider that owns this channel and writes the provider-local
// index to outLocalIdx. Returns nullptr if the channel isn't registered.
AdcProvider* findProvider(adc_channel_e channel, size_t& outLocalIdx) {
	if (!isAdcChannelValid(channel)) {
		return nullptr;
	}

	size_t globalIdx = channel - EFI_ADC_0;

	for (size_t i = 0; i < s_providerCount; i++) {
		auto& entry = s_providers[i];
		if (globalIdx >= entry.firstChannelIndex && globalIdx < entry.firstChannelIndex + entry.numChannels) {
			outLocalIdx = globalIdx - entry.firstChannelIndex;
			return entry.provider;
		}
	}

	return nullptr;
}
} // namespace

void registerAdcProvider(AdcProvider& provider, size_t firstChannelIndex, size_t numChannels) {
	if (s_providerCount >= MAX_PROVIDERS) {
		firmwareError(ObdCode::CUSTOM_ERR_ASSERT, "too many ADC providers registering %s", provider.name());
		return;
	}

	size_t newEnd = firstChannelIndex + numChannels;
	for (size_t i = 0; i < s_providerCount; i++) {
		auto& existing = s_providers[i];
		size_t existingEnd = existing.firstChannelIndex + existing.numChannels;
		if (firstChannelIndex < existingEnd && existing.firstChannelIndex < newEnd) {
			firmwareError(
					ObdCode::CUSTOM_ERR_ASSERT,
					"ADC provider %s overlaps %s",
					provider.name(),
					existing.provider->name());
			return;
		}
	}

	s_providers[s_providerCount++] = {&provider, firstChannelIndex, numChannels};
}

float AdcProvider::getVoltage(adc_channel_e channel) {
	size_t localIdx;
	auto provider = findProvider(channel, localIdx);
	if (!provider) {
		return 0;
	}

	return provider->get(localIdx);
}

bool AdcProvider::acquire(adc_channel_e channel) {
	size_t localIdx;
	auto provider = findProvider(channel, localIdx);
	if (!provider) {
		return false;
	}

	return provider->enable("adc", localIdx);
}

void AdcProvider::release(adc_channel_e channel) {
	size_t localIdx;
	auto provider = findProvider(channel, localIdx);
	if (!provider) {
		return;
	}

	provider->disable(localIdx);
}
