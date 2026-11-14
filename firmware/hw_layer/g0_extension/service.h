#pragma once

#include "link.h"

namespace g0 {
// Attach modules during board initialization, before start().
Link& link();
bool start(SPIDriver& driver, const SPIConfig& spiSettings);
uint32_t nowMs();

template <class T>
struct LockedModule final : Module {
	T state;
	bool request(wire::AppFrame& tx, uint32_t now) override {
		chibios_rt::CriticalSectionLocker lock;
		return state.request(tx, now);
	}
	void response(const wire::AppFrame& tx, const wire::AppFrame& rx, uint32_t now) override {
		chibios_rt::CriticalSectionLocker lock;
		state.response(tx, rx, now);
	}
	void disconnected() override {
		chibios_rt::CriticalSectionLocker lock;
		state.disconnected();
	}
};
} // namespace g0

// Implemented by the board, called after initSpiModules and before GPIO/sensors.
void initG0Extension();
