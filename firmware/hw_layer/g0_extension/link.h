#pragma once

#include "g0_spi_protocol.h"
#include <stddef.h>
#include <stdint.h>

namespace g0 {
namespace wire = g0_spi_protocol;

constexpr uint32_t SampleTimeoutMs = 250;

// All module callbacks run on the SPI worker. Adapters must lock shared state
// in these callbacks and in readers/writers, never across an SPI transfer.
struct Module {
	virtual bool request(wire::AppFrame& tx, uint32_t nowMs) = 0;
	virtual void response(const wire::AppFrame& tx, const wire::AppFrame& rx, uint32_t nowMs) = 0;
	virtual void disconnected() = 0;
};

class Link {
public:
	bool attach(Module& module);
	void prepare(wire::AppFrame& tx, uint32_t nowMs);
	void complete(const wire::AppFrame& rx, bool transferred, uint32_t nowMs);
	bool online() const {
		return m_online;
	}
	uint32_t errors() const {
		return m_errors;
	}

private:
	void disconnect();
	static constexpr size_t MaxModules = 4;
	Module* m_modules[MaxModules] = {};
	size_t m_count = 0;
	size_t m_next = 0;
	Module* m_pendingModule = nullptr;
	Module* m_sendingModule = nullptr;
	wire::AppFrame m_pending = {};
	wire::AppFrame m_sending = {};
	bool m_hasPending = false;
	bool m_online = false;
	uint32_t m_errors = 0;
	uint32_t m_pendingTime = 0;
};

bool validResponse(const wire::AppFrame& tx, const wire::AppFrame& rx);
} // namespace g0
