#include "link.h"

namespace g0 {
bool validResponse(const wire::AppFrame& tx, const wire::AppFrame& rx) {
	if (!wire::validFrame(rx)) {
		return false;
	}
	const auto& h = rx.responseHeader;
	if (h.status != wire::statusReady || h.result != wire::resultOk || h.command != tx.bytes[0]) {
		return false;
	}
	uint8_t size;
	switch (tx.bytes[0]) {
		case wire::cmdNop:
			size = 0;
			break;
		case wire::cmdReadVersion:
			size = wire::versionPayloadLength;
			break;
		case wire::cmdReadAnalog:
			size = wire::analogPayloadLength;
			break;
		case wire::cmdReadDigitalAll:
			size = wire::digitalAllPayloadLength;
			break;
		case wire::cmdReadDigital:
		case wire::cmdSetInputMode:
			size = wire::digitalPayloadLength;
			if (rx.digitalResponse.input != tx.bytes[1]) {
				return false;
			}
			break;
		case wire::cmdReadSent:
			size = wire::sentPayloadLength;
			if (rx.sentResponse.input != tx.bytes[1]) {
				return false;
			}
			break;
		case wire::cmdReadOutput:
		case wire::cmdSetOutput:
		case wire::cmdDisableOutput:
			size = wire::outputPayloadLength;
			if (rx.outputResponse.output != tx.bytes[1]) {
				return false;
			}
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
	tx = {};
	m_sendingModule = nullptr;
	if (!m_online) {
		tx.bytes[0] = wire::cmdReadVersion;
	} else {
		// Advance after every slot, even when a module has nothing to send.
		// Continuously changing outputs cannot starve sensor polls.
		for (size_t i = 0; i < m_count; i++) {
			auto* module = m_modules[m_next];
			m_next = (m_next + 1) % m_count;
			if (module->request(tx, nowMs)) {
				m_sendingModule = module;
				break;
			}
			tx = {};
		}
		if (!m_sendingModule) {
			tx.bytes[0] = wire::cmdReadVersion;
		}
	}
	wire::seal(tx);
	m_sending = tx;
}

void Link::disconnect() {
	m_online = false;
	m_hasPending = false;
	m_pendingModule = nullptr;
	for (size_t i = 0; i < m_count; i++) {
		m_modules[i]->disconnected();
	}
}

void Link::complete(const wire::AppFrame& rx, bool transferred, uint32_t nowMs) {
	if (!transferred) {
		m_errors++;
		disconnect();
		return;
	}
	if (m_hasPending) {
		if (uint32_t(nowMs - m_pendingTime) >= SampleTimeoutMs || !validResponse(m_pending, rx)) {
			m_errors++;
			disconnect();
			// The request sent during the failed exchange may still be executed.
			// Discard its reply, then re-establish the version pipeline.
			return;
		}
		if (m_pending.bytes[0] == wire::cmdReadVersion) {
			if (rx.versionResponse.protocolVersion != wire::protocolVersion || rx.versionResponse.version == 0) {
				m_errors++;
				disconnect();
				return;
			}
			m_online = true;
		} else if (m_pendingModule) {
			m_pendingModule->response(m_pending, rx, nowMs);
		}
	}
	m_pending = m_sending;
	m_pendingModule = m_sendingModule;
	m_hasPending = true;
	m_pendingTime = nowMs;
}
} // namespace g0
