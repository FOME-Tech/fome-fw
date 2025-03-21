/**
 * @file Lps25Sensor.cpp
 */

#include "pch.h"

#include "Lps25Sensor.h"

Lps25Sensor::Lps25Sensor(Lps25& sensor)
	: StoredValueSensor(SensorType::BarometricPressure, MS2NT(1000))
	, m_sensor(&sensor)
{
}

#ifdef STM32H7XX
Lps25TempSensor::Lps25TempSensor(Lps25& sensor)
	: StoredValueSensor(SensorType::LPSTemp, MS2NT(1000))
	, m_sensor(&sensor)
{
}
#endif

void Lps25Sensor::update() {
	if (auto result = m_sensor->readPressureKpa()) {
		setValidValue(result.Value, getTimeNowNt());
	} else {
		invalidate();
	}
}

#ifdef STM32H7XX
void Lps25TempSensor::update() {
	if (auto result = m_sensor->readLPSTemp()) {
		setValidValue(result.Value, getTimeNowNt());
	} else {
		invalidate();
	}
}
#endif
