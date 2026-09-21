#pragma once

#include <cstdint>

#include "stored_value_sensor.h"

// Hella OPS+T sends a three-symbol PWM frame: diagnosis/sync, temperature,
// then absolute oil pressure. Values are encoded as duty cycle so the decoder
// normalizes every data pulse by its measured period.
class HellaOpsTSensor {
public:
	HellaOpsTSensor()
		: m_pressure(SensorType::OilPressure, MS2NT(500))
		, m_temperature(SensorType::OilTemperature, MS2NT(500)) {}

	void init(brain_pin_e pin);
	void deInit();

	void onEdge(efitick_t nowNt);
	void onEdge(efitick_t nowNt, bool value);

private:
	enum class NextSymbol : uint8_t {
		None,
		Temperature,
		Pressure,
	};

	enum class Diagnostic : uint8_t {
		Ok,
		PressureFault,
		TemperatureFault,
		HardwareFault,
	};

	void decodeSymbol(efitick_t nowNt, float periodUs, float pulseUs);
	void resetDecoder();
	bool isFaulted(bool temperature) const;

	brain_pin_e m_pin = Gpio::Unassigned;
	StoredValueSensor m_pressure;
	StoredValueSensor m_temperature;
	Timer m_pulseTimer;
	Timer m_periodTimer;
	float m_pulseUs = -1;
	NextSymbol m_nextSymbol = NextSymbol::None;
	Diagnostic m_diagnostic = Diagnostic::Ok;
	bool m_pressureRegistered = false;
	bool m_temperatureRegistered = false;
	bool m_extiEnabled = false;
};
