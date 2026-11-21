#include "link.h"

namespace g0 {
bool validResponse(const wire::AppFrame& tx, const wire::AppFrame& rx) {
	const auto& h = rx.responseHeader;
	if (!wire::validFrame(rx) || h.status != wire::statusReady || h.result != wire::resultOk ||
		h.command != tx.bytes[0]) {
		return false;
	}
	uint8_t size;
	switch (tx.bytes[0]) {
		case wire::cmdReadVersion:
			if (rx.versionResponse.protocolVersion != wire::protocolVersion || rx.versionResponse.version == 0) {
				return false;
			}
			size = wire::versionPayloadLength;
			break;
		case wire::cmdReadAnalog:
			size = wire::analogPayloadLength;
			break;
		case wire::cmdReadDigitalAll:
			size = wire::digitalAllPayloadLength;
			break;
		case wire::cmdSetInputMode:
			if (rx.digitalResponse.input != tx.bytes[1]) {
				return false;
			}
			size = wire::digitalPayloadLength;
			break;
		case wire::cmdSetOutput:
			if (rx.outputResponse.output != tx.bytes[1]) {
				return false;
			}
			size = wire::outputPayloadLength;
			break;
		default:
			return false;
	}
	return h.payloadLength == size;
}

bool Link::attach(Module& module) {
	for (size_t i = 0; i < m_count; i++) {
		if (m_modules[i] == &module) {
			return true;
		}
	}
	if (m_count == MaxModules) {
		return false;
	}
	m_modules[m_count++] = &module;
	return true;
}

void Link::prepare(wire::AppFrame& tx, uint32_t nowMs) {
	m_sendingModule = nullptr;
	// Rotate even when a module has nothing to send, so outputs cannot starve sensors.
	for (size_t i = 0; m_online && i < m_count; i++) {
		auto* module = m_modules[m_next];
		m_next = (m_next + 1) % m_count;
		tx = {};
		if (module->request(tx, nowMs)) {
			m_sendingModule = module;
			break;
		}
	}
	if (!m_sendingModule) {
		tx = {};
		tx.bytes[0] = wire::cmdReadVersion;
	}
	wire::seal(tx);
}

void Link::disconnect() {
	m_online = false;
	m_hasPending = false;
	m_pendingModule = nullptr;
	for (size_t i = 0; i < m_count; i++) {
		m_modules[i]->disconnected();
	}
}

void Link::complete(const wire::AppFrame& tx, const wire::AppFrame& rx, bool transferred, uint32_t nowMs) {
	if (!transferred) {
		disconnect();
		return;
	}
	if (m_hasPending) {
		if (uint32_t(nowMs - m_pendingTime) >= SampleTimeoutMs || !validResponse(m_pending, rx)) {
			disconnect();
			// The request just sent may execute: discard its reply before checking the version again.
			return;
		}
		if (m_pending.bytes[0] == wire::cmdReadVersion) {
			m_online = true;
		} else if (m_pendingModule) {
			m_pendingModule->response(m_pending, rx, nowMs);
		}
	}
	m_pending = tx;
	m_pendingModule = m_sendingModule;
	m_hasPending = true;
	m_pendingTime = nowMs;
}
} // namespace g0
