#pragma once

#include "stored_value_sensor.h"

/**
 * Hella PULS ultrasonic oil level/temperature sensor.
 *
 * The sensor emits a repeating sequence of three positive pulses on a single open collector
 * output. Pulse width encodes the value, and the position in the sequence selects which
 * quantity is being reported:
 *
 *   T1 (temperature) -> 110ms -> T2 (level) -> 110ms -> T3 (diagnostic) -> 780ms -> repeat
 *
 * Pulse widths run from 22ms (20% of the 110ms window) to 88ms (80%), spanning the full
 * measurement range of whichever quantity that pulse carries. The 68.2ms diagnostic pulse
 * is fixed width and is not currently decoded.
 *
 * Sync is acquired on the 780ms rising-edge-to-rising-edge gap, which only ever follows the
 * diagnostic pulse, so the pulse after it is unambiguously temperature.
 */
class HellaOilLevelSensor : public StoredValueSensor {
public:
	HellaOilLevelSensor(SensorType levelType, SensorType temperatureType)
		: StoredValueSensor(levelType, MS2NT(2000))
		, m_temperature(temperatureType, MS2NT(2000)) {}

	void init(brain_pin_e pin, hella_oil_level_variant_e variant);
	void deInit();

	void onEdge(efitick_t nowNt);
	void onEdge(efitick_t nowNt, bool value);

private:
	// Temperature shares the same signal, so it is driven from the same edge callback
	StoredValueSensor m_temperature;

	// Whether we own the temperature registration - an analog sender may have claimed it first
	bool m_temperatureRegistered = false;

	brain_pin_e m_pin = Gpio::Unassigned;

	// Level reported at minimum (22ms) and maximum (88ms) pulse width, in millimeters.
	// Set from the configured sensor variant.
	float m_levelAtMinPulse = 0;
	float m_levelAtMaxPulse = 0;

	// Measures the width of positive pulses (rising -> falling)
	Timer m_pulseTimer;

	// Measures the time between pulses (rising -> rising)
	Timer m_betweenPulseTimer;

	enum class NextPulse {
		None,
		Temp,
		Level,
		Diag
	};
	NextPulse m_nextPulse = NextPulse::None;
};
