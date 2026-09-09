#include "pch.h"

#include "hella_opst.h"

#include "digital_input_exti.h"

namespace {
constexpr float SyncPeriodUs = 1024;
constexpr float DataPeriodUs = 4096;
constexpr float PeriodTolerance = 0.10f;
constexpr float MinDataPulseUs = 128;
constexpr float MaxDataPulseUs = 3968;
constexpr float StandardBaroKpa = 101.325f;

bool isNearPeriod(float periodUs, float nominalUs) {
	return periodUs >= nominalUs * (1 - PeriodTolerance) && periodUs <= nominalUs * (1 + PeriodTolerance);
}

void hellaOpsTExtiCallback(void* arg, efitick_t nowNt) {
	reinterpret_cast<HellaOpsTSensor*>(arg)->onEdge(nowNt);
}
} // namespace

void HellaOpsTSensor::init(brain_pin_e pin) {
	deInit();

	const bool needsPressure = !Sensor::hasSensor(SensorType::OilPressure);
	const bool needsTemperature = !Sensor::hasSensor(SensorType::OilTemperature);

	if (!isBrainPinValid(pin) || (!needsPressure && !needsTemperature)) {
		return;
	}

	if (needsPressure) {
		m_pressureRegistered = m_pressure.Register();
	}

	if (needsTemperature) {
		m_temperatureRegistered = m_temperature.Register();
	}

	if (!m_pressureRegistered && !m_temperatureRegistered) {
		return;
	}

	m_pin = pin;
	resetDecoder();

#if EFI_PROD_CODE
	if (!efiExtiEnablePin(
				"Hella OPS+T", pin, PAL_EVENT_MODE_BOTH_EDGES, hellaOpsTExtiCallback, reinterpret_cast<void*>(this))) {
		deInit();
		return;
	}
	m_extiEnabled = true;
#endif // EFI_PROD_CODE
}

void HellaOpsTSensor::deInit() {
	if (m_extiEnabled) {
#if EFI_PROD_CODE
		efiExtiDisablePin(m_pin);
#endif // EFI_PROD_CODE
		m_extiEnabled = false;
	}

	if (m_pressureRegistered) {
		m_pressure.unregister();
		m_pressureRegistered = false;
	}

	if (m_temperatureRegistered) {
		m_temperature.unregister();
		m_temperatureRegistered = false;
	}

	m_pin = Gpio::Unassigned;
	resetDecoder();
}

void HellaOpsTSensor::onEdge(efitick_t nowNt) {
#if EFI_PROD_CODE
	onEdge(nowNt, efiReadPin(m_pin));
#else
	UNUSED(nowNt);
#endif // EFI_PROD_CODE
}

void HellaOpsTSensor::onEdge(efitick_t nowNt, bool value) {
	if (value) {
		const float periodUs = m_periodTimer.getElapsedUs(nowNt);
		m_periodTimer.reset(nowNt);

		if (m_pulseUs >= 0) {
			decodeSymbol(nowNt, periodUs, m_pulseUs);
		}

		m_pulseTimer.reset(nowNt);
		m_pulseUs = -1;
	} else {
		m_pulseUs = m_pulseTimer.getElapsedUs(nowNt);
	}
}

bool HellaOpsTSensor::isFaulted(bool temperature) const {
	return m_diagnostic == Diagnostic::HardwareFault ||
			(temperature && m_diagnostic == Diagnostic::TemperatureFault) ||
			(!temperature && m_diagnostic == Diagnostic::PressureFault);
}

void HellaOpsTSensor::decodeSymbol(efitick_t nowNt, float periodUs, float pulseUs) {
	if (pulseUs <= 0 || pulseUs >= periodUs) {
		m_nextSymbol = NextSymbol::None;
		return;
	}

	if (isNearPeriod(periodUs, SyncPeriodUs)) {
		const float diagnostic = 256 * pulseUs / periodUs;

		if (diagnostic >= 56 && diagnostic <= 72) {
			m_diagnostic = Diagnostic::Ok;
		} else if (diagnostic >= 88 && diagnostic <= 104) {
			m_diagnostic = Diagnostic::PressureFault;
		} else if (diagnostic >= 120 && diagnostic <= 136) {
			m_diagnostic = Diagnostic::TemperatureFault;
		} else if (diagnostic >= 152 && diagnostic <= 168) {
			m_diagnostic = Diagnostic::HardwareFault;
		} else {
			m_nextSymbol = NextSymbol::None;
			return;
		}

		m_nextSymbol = NextSymbol::Temperature;
		return;
	}

	if (!isNearPeriod(periodUs, DataPeriodUs) || m_nextSymbol == NextSymbol::None) {
		m_nextSymbol = NextSymbol::None;
		return;
	}

	const bool temperature = m_nextSymbol == NextSymbol::Temperature;
	StoredValueSensor& sensor = temperature ? m_temperature : m_pressure;
	const bool registered = temperature ? m_temperatureRegistered : m_pressureRegistered;
	const float normalizedPulseUs = DataPeriodUs * pulseUs / periodUs;

	if (registered) {
		if (normalizedPulseUs < MinDataPulseUs) {
			sensor.invalidate(UnexpectedCode::Low);
			m_nextSymbol = NextSymbol::None;
			return;
		} else if (normalizedPulseUs > MaxDataPulseUs) {
			sensor.invalidate(UnexpectedCode::High);
			m_nextSymbol = NextSymbol::None;
			return;
		} else if (isFaulted(temperature)) {
			sensor.invalidate(UnexpectedCode::Inconsistent);
		} else if (temperature) {
			sensor.setValidValue(((normalizedPulseUs - 128) / 19.2f) - 40, nowNt);
		} else {
			const float absoluteKpa = ((normalizedPulseUs - 128) / 384 + 0.5f) * 100;
			const float baroKpa = Sensor::get(SensorType::BarometricPressure).value_or(StandardBaroKpa);
			sensor.setValidValue(absoluteKpa > baroKpa ? absoluteKpa - baroKpa : 0, nowNt);
		}
	}

	m_nextSymbol = temperature ? NextSymbol::Pressure : NextSymbol::None;
}

void HellaOpsTSensor::resetDecoder() {
	m_pulseUs = -1;
	m_nextSymbol = NextSymbol::None;
	m_diagnostic = Diagnostic::Ok;
}
