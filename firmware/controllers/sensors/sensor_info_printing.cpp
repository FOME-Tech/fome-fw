#include "pch.h"
#include "proxy_sensor.h"
#include "functional_sensor.h"
#include "redundant_sensor.h"
#include "fallback_sensor.h"
#include "frequency_sensor.h"
#include "Lps25Sensor.h"
#include "linear_func.h"
#include "resistance_func.h"
#include "thermistor_func.h"
#include "identity_func.h"

void ProxySensor::showInfo(const char* sensorName) const {
	efiPrintf("Sensor \"%s\" proxied from sensor \"%s\"", sensorName, getSensorName(m_proxiedSensor));
}

void FunctionalSensor::showInfo(const char* sensorName) const {
	const auto value = get();
	efiPrintf("Sensor \"%s\": Raw value: %.2f Valid: %s Converted value %.2f", sensorName, m_rawValue, boolToString(value.Valid), value.Value);

	// now print out the underlying function's info
	if (auto func = m_function) {
		func->showInfo(m_rawValue);
	}
}

#if EFI_CAN_SUPPORT || EFI_UNIT_TEST
#include "can_sensor.h"

void CanSensorBase::showInfo(const char* sensorName) const {
	const auto value = get();
	efiPrintf("CAN Sensor \"%s\": valid: %s value: %.2f", sensorName, boolToString(value.Valid), value.Value);
}
#endif // EFI_CAN_SUPPORT

void RedundantSensor::showInfo(const char* sensorName) const {
	efiPrintf("Sensor \"%s\" is redundant combining \"%s\" and \"%s\": primary only: %s partial secondary: %s (max: %f)",
		sensorName,
		getSensorName(m_first),
		getSensorName(m_second),
		boolToString(m_ignoreSecond),
		boolToString(m_secondMaximum < 100),
		m_secondMaximum
	);
}

void FrequencySensor::showInfo(const char* sensorName) const {
	efiPrintf("FrequencySensor \"%s\" counter %d", sensorName, eventCounter);
}

void FallbackSensor::showInfo(const char* sensorName) const {
	efiPrintf("Sensor \"%s\" is fallback sensor with primary \"%s\" and fallback \"%s\"", sensorName, getSensorName(m_primary), getSensorName(m_fallback));
}

void RpmCalculator::showInfo(const char* /*sensorName*/) const {
#if EFI_SHAFT_POSITION_INPUT
	efiPrintf("RPM sensor: stopped: %d spinning up: %d cranking: %d running: %d rpm: %f", 
		isStopped(),
		isSpinningUp(),
		isCranking(),
		isRunning(),
		get().value_or(0)
	);
#endif // EFI_SHAFT_POSITION_INPUT
}

void Lps25Sensor::showInfo(const char* sensorName) const {
	efiPrintf("%s: LPS25 baro %.2f kPa", sensorName, get().Value);
}

void MapAverager::showInfo(const char* sensorName) const {
	const auto value = get();
	efiPrintf("Sensor \"%s\" is MAP averager: valid: %s value: %.2f averaged sample count: %d", sensorName, boolToString(value.Valid), value.Value, m_lastCounter);
}

void LinearFunc::showInfo(float testRawValue) const {
	efiPrintf("    Linear function slope: %.2f offset: %.2f min: %.1f max: %.1f", m_a, m_b, m_minOutput, m_maxOutput);
	const auto value = convert(testRawValue);
	efiPrintf("      raw value %.2f converts to %.2f valid: %s", testRawValue, value.Value, boolToString(value.Valid));
}

void ResistanceFunc::showInfo(float testInputValue) const {
	const auto result = convert(testInputValue);
	efiPrintf("    %.2f volts -> %.1f ohms with supply voltage %.2f and pullup %.1f.", testInputValue, result.Value, m_supplyVoltage, m_pullupResistor);
}

void ThermistorFunc::showInfo(float testInputValue) const {
	const auto value = convert(testInputValue);
	efiPrintf("    %.1f ohms -> valid: %s. %.1f deg C", testInputValue, boolToString(value.Valid), value.Value);
}

void IdentityFunction::showInfo(float /*testInputValue*/) const {
	efiPrintf("    Identity function passes along value.");
}
