#pragma once
#include "link.h"

namespace g0 {
class Outputs : public Module {
public:
	bool set(size_t index, uint32_t frequency, uint16_t duty) {
		if (index >= wire::outputCount || frequency > 500 || duty > wire::outputDutyMax) {
			return false;
		}
		auto& value = m_values[index];
		value.dirty |= value.frequency != frequency || value.duty != duty;
		value.frequency = frequency;
		value.duty = duty;
		return true;
	}
	bool request(wire::AppFrame& tx, uint32_t nowMs) override {
		for (size_t i = 0; i < wire::outputCount; i++) {
			const size_t index = (m_next + i) % wire::outputCount;
			auto& value = m_values[index];
			if (value.pending || (!value.dirty && uint32_t(nowMs - value.updated) < 1000)) {
				continue;
			}
			tx.setOutputRequest = {wire::cmdSetOutput, uint8_t(index + 1), value.frequency, value.duty};
			value.pending = true;
			m_next = (index + 1) % wire::outputCount;
			return true;
		}
		return false;
	}
	void response(const wire::AppFrame& tx, const wire::AppFrame& rx, uint32_t nowMs) override {
		const size_t index = tx.bytes[1] - 1;
		if (index >= wire::outputCount) {
			return;
		}
		auto& value = m_values[index];
		const auto& sent = tx.setOutputRequest;
		value.pending = false;
		value.dirty = rx.outputResponse.frequencyHz != sent.frequencyHz || rx.outputResponse.duty != sent.duty ||
					  value.frequency != sent.frequencyHz || value.duty != sent.duty;
		value.updated = nowMs;
	}
	void disconnected() override {
		for (auto& value : m_values) {
			value.pending = false;
			value.dirty = true;
		}
	}

private:
	struct State {
		uint32_t frequency = 0;
		uint16_t duty = 0;
		bool dirty = true;
		bool pending = false;
		uint32_t updated = 0;
	};
	State m_values[wire::outputCount];
	size_t m_next = 0;
};
} // namespace g0
