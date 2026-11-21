#include "pch.h"
#include "analog.h"
#include "service.h"
#include "adc_provider.h"

namespace g0 {
namespace {
LockedModule<Analog> analog;
class Provider final : public AdcProvider {
	const char* name() const override {
		return "G0";
	}
	bool enable(const char*, size_t index) override {
		return index < wire::analogChannelCount;
	}
	void disable(size_t) override {}
	float get(size_t index) const override {
		chibios_rt::CriticalSectionLocker lock;
		return analog.state.voltage(index, nowMs());
	}
};
Provider provider;
} // namespace

void registerAnalog(size_t firstChannelIndex) {
	registerAdcProvider(provider, firstChannelIndex, wire::analogChannelCount);
	link().attach(analog);
}
} // namespace g0
