#pragma once
#include "link.h"

namespace g0 {
// The slave rejects SENT until a validated decoder is available.
enum class InputMode : uint8_t {
	Digital = 0,
	Frequency = 1
};

class Digital : public Module {
public:
	bool setMode(size_t index, InputMode mode) {
		if (index >= wire::digitalInputCount || uint8_t(mode) > uint8_t(InputMode::Frequency)) {
			return false;
		}
		if (m_modes[index] != mode) {
			m_modes[index] = mode;
			m_configured[index] = false;
			m_ready = false;
		}
		return true;
	}
	bool request(wire::AppFrame& tx, uint32_t) override {
		for (size_t i = 0; i < wire::digitalInputCount; i++) {
			if (m_configured[i]) {
				continue;
			}
			tx.setInputModeRequest = {wire::cmdSetInputMode, uint8_t(i + 1), uint8_t(m_modes[i])};
			return true;
		}
		tx.bytes[0] = wire::cmdReadDigitalAll;
		return true;
	}
	void response(const wire::AppFrame& tx, const wire::AppFrame& rx, uint32_t nowMs) override {
		if (tx.bytes[0] == wire::cmdSetInputMode) {
			const size_t index = tx.setInputModeRequest.input - 1;
			if (index >= wire::digitalInputCount) {
				return;
			}
			m_configured[index] = rx.digitalResponse.mode == tx.setInputModeRequest.mode &&
								  rx.digitalResponse.mode == uint8_t(m_modes[index]);
			return;
		}
		m_ready = false;
		if (rx.digitalAllResponse.inputCount != wire::digitalInputCount) {
			return;
		}
		for (size_t i = 0; i < wire::digitalInputCount; i++) {
			const auto& input = rx.digitalAllResponse.inputs[i];
			if (input.mode != uint8_t(m_modes[i]) || input.dutyPermille > 1000 || (input.flags & ~3U)) {
				return;
			}
			m_inputs[i] = input;
		}
		m_updated = nowMs;
		m_ready = true;
	}
	void disconnected() override {
		m_ready = false;
		for (auto& configured : m_configured) {
			configured = false;
		}
	}
	bool sample(size_t index, uint32_t nowMs, wire::DigitalAllState& sample) const {
		if (index >= wire::digitalInputCount || !m_ready || uint32_t(nowMs - m_updated) >= SampleTimeoutMs) {
			return false;
		}
		sample = m_inputs[index];
		return true;
	}

private:
	InputMode m_modes[wire::digitalInputCount] = {};
	bool m_configured[wire::digitalInputCount] = {};
	wire::DigitalAllState m_inputs[wire::digitalInputCount] = {};
	uint32_t m_updated = 0;
	bool m_ready = false;
};
void registerDigital();
} // namespace g0
