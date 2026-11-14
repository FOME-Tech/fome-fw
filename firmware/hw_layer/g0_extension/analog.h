#pragma once
#include "link.h"
#include <cmath>

namespace g0 {
class Analog : public Module {
public:
	bool request(wire::AppFrame& tx, uint32_t) override {
		tx.bytes[0] = wire::cmdReadAnalog;
		return true;
	}
	void response(const wire::AppFrame&, const wire::AppFrame& rx, uint32_t nowMs) override {
		const auto& value = rx.analogResponse;
		m_ready = false;
		if (value.ready != 1 || value.channelCount != wire::analogChannelCount) {
			return;
		}
		for (size_t i = 0; i < wire::analogChannelCount; i++) {
			if (value.millivolts[i] > 3300) {
				return;
			}
			m_millivolts[i] = value.millivolts[i];
		}
		m_updated = nowMs;
		m_ready = true;
	}
	void disconnected() override {
		m_ready = false;
	}
	float voltage(size_t index, uint32_t nowMs) const {
		if (index >= wire::analogChannelCount || !m_ready || uint32_t(nowMs - m_updated) >= SampleTimeoutMs) {
			return NAN;
		}
		return m_millivolts[index] * 0.001f;
	}

private:
	bool m_ready = false;
	uint32_t m_updated = 0;
	uint16_t m_millivolts[wire::analogChannelCount] = {};
};
void registerAnalog(size_t firstChannelIndex);
} // namespace g0
