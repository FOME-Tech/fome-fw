/**
 * @file frequency_sensor.cpp
 */

#include "pch.h"

#include "frequency_sensor.h"

#include "digital_input_exti.h"

#include <atomic>

static constexpr float ticksPerSecond = NT_PER_SECOND;

// How often updateAll() is called
static constexpr efidur_t updatePeriod = MS2NT(FAST_CALLBACK_PERIOD_MS);

FrequencySensor* FrequencySensor::s_first = nullptr;

FrequencySensor::FrequencySensor(SensorType type, efidur_t timeoutPeriod)
	: FunctionalSensor(type, timeoutPeriod) {
	m_next = s_first;
	s_first = this;
}

FrequencySensor::~FrequencySensor() {
	for (auto link = &s_first; *link; link = &(*link)->m_next) {
		if (*link == this) {
			*link = m_next;
			break;
		}
	}
}

void FrequencySensor::initIfValid(brain_pin_e pin, SensorConverter& converter, uint8_t windowMs) {
	if (!isBrainPinValid(pin)) {
		return;
	}

#if EFI_PROD_CODE
	// No callback: the fast ISR only counts edges, and update() reads the count
	if (!efiExtiEnablePin(getSensorName(), pin, PAL_EVENT_MODE_FALLING_EDGE, nullptr, nullptr)) {
		// Don't register, so that a pin conflict reads as a failed sensor, not a stopped one
		return;
	}
#endif // EFI_PROD_CODE

	m_enabledPin = pin;
	setFunction(converter);
	requestReconfigure(pin, windowMs);

	Register();
}

void FrequencySensor::deInit() {
	brain_pin_e pin = m_enabledPin;

	if (!isBrainPinValid(pin)) {
		return;
	}

	m_enabledPin = Gpio::Unassigned;
	requestReconfigure(Gpio::Unassigned, 0);

#if EFI_PROD_CODE
	efiExtiDisablePin(pin);
#endif
}

void FrequencySensor::requestReconfigure(brain_pin_e pin, uint8_t windowMs) {
	m_pendingPin = pin;
	m_pendingWindowMs = windowMs;

	// update() runs on the same core, so keeping the compiler from moving the stores above past the
	// flag is enough
	std::atomic_signal_fence(std::memory_order_release);
	m_reconfigure = true;
}

void FrequencySensor::applyReconfigure(efitick_t nowNt) {
	m_pin = m_pendingPin;
	m_window = MS2NT(m_pendingWindowMs);

	// Pushes only happen on updates, so the newest snapshot's edge can be up to one update older than
	// the push, and RingSize snapshots span RingSize - 1 strides. Round up, so that the ring always
	// reaches back at least a whole window.
	m_stride = (m_window + updatePeriod + RingSize - 2) / (RingSize - 1);

	m_ringCount = 0;

	// The count is free-running, so start from wherever it is now
	m_lastCount = readCounter(m_pin).Count;
	m_lastChangeNt = nowNt;
}

void FrequencySensor::updateAll(efitick_t nowNt) {
	for (auto sensor = s_first; sensor; sensor = sensor->m_next) {
		sensor->update(nowNt);
	}
}

void FrequencySensor::update(efitick_t nowNt) {
	if (m_reconfigure) {
		// Clear the flag before reading the settings: if they change again meanwhile, the flag is set
		// again and the next update applies them
		m_reconfigure = false;
		std::atomic_signal_fence(std::memory_order_acquire);

		applyReconfigure(nowNt);
	}

	if (!isBrainPinValid(m_pin)) {
		return;
	}

	Snapshot now = readCounter(m_pin);

	if (now.Count == 0 && now.LastEdge == 0) {
		// No edge yet since boot (or the pin was disabled under us)
		postRawValue(0, nowNt);
		return;
	}

	if (now.Count != m_lastCount) {
		m_lastCount = now.Count;
		m_lastChangeNt = nowNt;

		if (m_ringCount == 0 || nowNt - m_lastPushNt >= m_stride) {
			push(now, nowNt);
		}
	}

	if (nowNt - m_lastChangeNt > StopTimeout) {
		// Input stopped. Forget the history so that the next window can't stretch back across the
		// stop, which also keeps every timestamp difference far from the 32-bit wrap.
		m_ringCount = 0;
		postRawValue(0, nowNt);
		return;
	}

	postRawValue(measure(now, static_cast<uint32_t>(nowNt.count)), nowNt);
}

void FrequencySensor::push(const Snapshot& snapshot, efitick_t nowNt) {
	m_ringHead = (m_ringHead + 1) % RingSize;
	m_ring[m_ringHead] = snapshot;

	if (m_ringCount < RingSize) {
		m_ringCount++;
	}

	m_lastPushNt = nowNt;
}

float FrequencySensor::measure(const Snapshot& now, uint32_t nowTicks) const {
	// Walk back from the newest snapshot until the window is covered, or the history runs out. The
	// newest snapshot may be this same edge, which can't start a window.
	const Snapshot* start = nullptr;

	for (size_t i = 0; i < m_ringCount; i++) {
		auto& candidate = m_ring[(m_ringHead + RingSize - i) % RingSize];

		if (candidate.Count == now.Count) {
			continue;
		}

		start = &candidate;

		if (now.LastEdge - candidate.LastEdge >= m_window) {
			break;
		}
	}

	if (!start) {
		// Fewer than two edges since the input (re)started
		return 0;
	}

	uint32_t edges = now.Count - start->Count;
	uint32_t period = now.LastEdge - start->LastEdge;

	if (period == 0) {
		return 0;
	}

	float frequency = edges * ticksPerSecond / period;

	// Once the next edge is late, the input is slower than the window says. Bounding by two edges
	// per time since the last one lets an edge be up to a whole period late (jitter, uneven teeth)
	// without pulling the reading down, while a real slowdown or stop still shows within a period
	// or two. A bound of one edge would turn every late edge into a dip. An edge that arrived after
	// nowTicks was sampled makes sinceLastEdge negative, so skip the bound then.
	int32_t sinceLastEdge = nowTicks - now.LastEdge;
	if (sinceLastEdge > 0) {
		frequency = std::min(frequency, 2 * ticksPerSecond / sinceLastEdge);
	}

	return frequency;
}

FrequencySensor::Snapshot FrequencySensor::readCounter([[maybe_unused]] brain_pin_e pin) const {
#if EFI_PROD_CODE
	auto counter = efiExtiGetCounter(pin);
	return {counter.Count, counter.LastEdgeTimestamp};
#elif EFI_UNIT_TEST
	return m_simCounter;
#else
	return {};
#endif
}

#if EFI_UNIT_TEST
void FrequencySensor::onEdge(efitick_t nowNt) {
	m_simCounter.Count++;
	m_simCounter.LastEdge = static_cast<uint32_t>(nowNt.count);
}
#endif
