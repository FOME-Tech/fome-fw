#include "pch.h"

#include "adc_provider.h"

namespace {
struct ProviderSlot {
	AdcProvider* provider = nullptr;
	size_t localIdx = 0;
};

constexpr size_t SLOT_COUNT = EFI_ADC_LAST_CHANNEL - EFI_ADC_0;

static ProviderSlot s_slots[SLOT_COUNT];

ProviderSlot* findSlot(adc_channel_e channel) {
	if (!isAdcChannelValid(channel)) {
		return nullptr;
	}

	size_t idx = channel - EFI_ADC_0;
	if (idx >= SLOT_COUNT) {
		return nullptr;
	}

	return &s_slots[idx];
}
} // namespace

void registerAdcProvider(AdcProvider& provider, size_t firstChannelIndex, size_t numChannels) {
	for (size_t i = 0; i < numChannels; i++) {
		size_t globalIdx = firstChannelIndex + i;

		if (globalIdx >= SLOT_COUNT) {
			firmwareError(
					ObdCode::CUSTOM_ERR_ASSERT,
					"%s: ADC channel index %u out of range",
					provider.name(),
					(unsigned)globalIdx);
			return;
		}

		s_slots[globalIdx] = {&provider, i};
	}
}

float AdcProvider::getVoltage(adc_channel_e channel) {
	auto slot = findSlot(channel);
	if (!slot || !slot->provider) {
		return 0;
	}

	return slot->provider->get(slot->localIdx);
}

bool AdcProvider::acquire(adc_channel_e channel) {
	auto slot = findSlot(channel);
	if (!slot || !slot->provider) {
		return false;
	}

	return slot->provider->enable("adc", slot->localIdx);
}

void AdcProvider::release(adc_channel_e channel) {
	auto slot = findSlot(channel);
	if (!slot || !slot->provider) {
		return;
	}

	slot->provider->disable(slot->localIdx);
}
