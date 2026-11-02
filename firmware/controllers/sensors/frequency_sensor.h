/**
 * @file frequency_sensor.h
 */

#pragma once

#include "functional_sensor.h"

/**
 * Measures the frequency of a digital input by reciprocal counting: the EXTI fast ISR only counts
 * edges and stamps the most recent one, and update() divides the number of edges by the time
 * between the first and last edge of a sliding window. Edges in the middle of the window only
 * count, so their timing jitter doesn't matter.
 *
 * When edges are further apart than the window, it stretches back to the previous edge. When the
 * input stops, the reported frequency decays, then latches to a valid zero after StopTimeout without
 * an edge.
 */
class FrequencySensor : public FunctionalSensor {
public:
	FrequencySensor(SensorType type, efidur_t timeoutPeriod);
	~FrequencySensor();

	void initIfValid(brain_pin_e pin, SensorConverter& converter, uint8_t windowMs);
	void deInit();

	// Recompute every frequency sensor, call every FAST_CALLBACK_PERIOD_MS from the main loop
	static void updateAll(efitick_t nowNt);

	void update(efitick_t nowNt);

	void showInfo(const char* sensorName) const override;

	uint32_t getEdgeCount() const {
		return m_lastCount;
	}

#if EFI_UNIT_TEST
	// Simulate an edge on the input, the same way the EXTI fast ISR counts it
	void onEdge(efitick_t nowNt);
#endif

private:
	struct Snapshot {
		uint32_t Count;
		// Lower 32 bits of the timer at the most recent edge
		uint32_t LastEdge;
	};

	void requestReconfigure(brain_pin_e pin, uint8_t windowMs);
	void applyReconfigure(efitick_t nowNt);
	Snapshot readCounter(brain_pin_e pin) const;
	void push(const Snapshot& snapshot, efitick_t nowNt);
	float measure(const Snapshot& now, uint32_t nowTicks) const;

	static constexpr efidur_t StopTimeout = MS2NT(2000);

	// initIfValid() and deInit() may run on another thread, so they only leave the new settings here,
	// and update() applies them. Everything below is then only touched by update().
	brain_pin_e m_pendingPin = Gpio::Unassigned;
	uint8_t m_pendingWindowMs = 0;
	volatile bool m_reconfigure = false;
	// The pin whose EXTI is enabled, owned by initIfValid() and deInit()
	brain_pin_e m_enabledPin = Gpio::Unassigned;

	// Snapshots taken when the edge count changed, at least m_stride apart, so the ring always
	// reaches back at least one window
	static constexpr size_t RingSize = 16;
	Snapshot m_ring[RingSize];
	size_t m_ringHead = 0;
	size_t m_ringCount = 0;

	uint32_t m_window = 0;
	efidur_t m_stride = 0;
	efitick_t m_lastPushNt = 0;
	efitick_t m_lastChangeNt = 0;
	uint32_t m_lastCount = 0;

	brain_pin_e m_pin = Gpio::Unassigned;

	FrequencySensor* m_next = nullptr;
	static FrequencySensor* s_first;

#if EFI_UNIT_TEST
	Snapshot m_simCounter = {};
#endif
};
