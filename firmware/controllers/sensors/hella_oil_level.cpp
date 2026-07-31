#include "pch.h"

#include "hella_oil_level.h"

#include "digital_input_exti.h"

// Pulse width at the bottom and top of any measured range: 20% and 80% of the 110ms window
static constexpr float minPulseMs = 22;
static constexpr float maxPulseMs = 88;

// Temperature range is the same for every variant
static constexpr float minTempC = -40;
static constexpr float maxTempC = 160;

static void hellaSensorExtiCallback(void* arg, efitick_t nowNt) {
	reinterpret_cast<HellaOilLevelSensor*>(arg)->onEdge(nowNt);
}

void HellaOilLevelSensor::init(brain_pin_e pin, hella_oil_level_variant_e variant) {
	if (!isBrainPinValid(pin)) {
		return;
	}

	switch (variant) {
		case HELLA_OIL_LEVEL_129MM:
			m_levelAtMinPulse = 18.0f;
			m_levelAtMaxPulse = 147.0f;
			break;
		case HELLA_OIL_LEVEL_45MM:
		default:
			m_levelAtMinPulse = 20.9f;
			m_levelAtMaxPulse = 65.9f;
			break;
	}

	m_pin = pin;

#if EFI_PROD_CODE
	efiExtiEnablePin(
			getSensorName(), pin, PAL_EVENT_MODE_BOTH_EDGES, hellaSensorExtiCallback, reinterpret_cast<void*>(this));
#endif // EFI_PROD_CODE

	Register();

	// Oil temperature may already be provided by an analog sender. Registering it twice is a
	// firmware error, so leave the existing sensor alone and report level only.
	if (!Sensor::hasSensor(m_temperature.type())) {
		m_temperatureRegistered = m_temperature.Register();
	}
}

void HellaOilLevelSensor::deInit() {
	if (!isBrainPinValid(m_pin)) {
		// Never initialized, so there's nothing of ours to tear down. Bail out rather than
		// clearing registry slots we don't own.
		return;
	}

#if EFI_PROD_CODE
	efiExtiDisablePin(m_pin);
#endif // EFI_PROD_CODE

	m_pin = Gpio::Unassigned;
	m_nextPulse = NextPulse::None;

	unregister();

	// Only give up the temperature slot if it was ours to begin with
	if (m_temperatureRegistered) {
		m_temperature.unregister();
		m_temperatureRegistered = false;
	}
}

void HellaOilLevelSensor::onEdge(efitick_t nowNt) {
#if EFI_PROD_CODE
	onEdge(nowNt, efiReadPin(m_pin));
#endif
}

void HellaOilLevelSensor::onEdge(efitick_t nowNt, bool value) {
	if (value) {
		// Start pulse width timing at the rising edge
		m_pulseTimer.reset(nowNt);

		float timeBetweenPulses = m_betweenPulseTimer.getElapsedSecondsAndReset(nowNt);

		if (timeBetweenPulses > 0.89 * 0.780 && timeBetweenPulses < 1.11 * 0.780) {
			// 780ms nominal between Diag and next Temp pulse start, +-10%
			// (68.2ms diag pulse plus the 711.8ms idle remainder of the 1000ms frame)

			// This was the "long gap" break, next pulse is temperature.
			m_nextPulse = NextPulse::Temp;
		} else if (timeBetweenPulses > 0.89 * 0.110 && timeBetweenPulses < 1.11 * 0.110) {
			// 110ms nominal between each pulse (other than break)

			// Advance the state machine to decode the next pulse in the sequence
			switch (m_nextPulse) {
				case NextPulse::Temp:
					m_nextPulse = NextPulse::Level;
					break;
				case NextPulse::Level:
					m_nextPulse = NextPulse::Diag;
					break;
				default:
					// We don't know how we got here, reset to safe state
					m_nextPulse = NextPulse::None;
					break;
			}
		} else {
			// The break was too long, ignore it for now.
			m_nextPulse = NextPulse::None;
		}
	} else {
		// Stop timing at the falling edge
		float lastPulseMs = 1000 * m_pulseTimer.getElapsedSeconds(nowNt);

		// Data pulses are 22ms to 88ms, and the fixed diagnostic pulse is 68.2ms. All time
		// signals carry a +-10% tolerance, so allow margin beyond the nominal range before
		// deciding a pulse is bogus. Anything outside means we lost the plot - resync.
		if (lastPulseMs > 100 || lastPulseMs < 20) {
			m_nextPulse = NextPulse::None;
			return;
		}

		switch (m_nextPulse) {
			case NextPulse::Temp: {
				float tempC = interpolateClamped(minPulseMs, minTempC, maxPulseMs, maxTempC, lastPulseMs);
				m_temperature.setValidValue(tempC, nowNt);
				break;
			}
			case NextPulse::Level: {
				float levelMm =
						interpolateClamped(minPulseMs, m_levelAtMinPulse, maxPulseMs, m_levelAtMaxPulse, lastPulseMs);
				setValidValue(levelMm, nowNt);
				break;
			}
			default:
				// Diagnostic pulse is fixed width and carries no measurement, and without sync
				// we don't know what this pulse means. Either way, nothing to record.
				break;
		}
	}
}
