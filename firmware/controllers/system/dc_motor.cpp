/**
 * @file DcMotor.cpp
 * @brief DC motor controller
 *
 * @date Dec 22, 2018
 * @author Matthew Kennedy
 */

#include "pch.h"

#include "dc_motor.h"

TwoPinDcMotor::TwoPinDcMotor(OutputPin& disablePin)
	: m_disable(&disablePin) {
	disable("init");
}

void TwoPinDcMotor::configure(IPwm& enable, IPwm& dir1, IPwm& dir2, bool isInverted) {
	m_enable = &enable;
	m_dir1 = &dir1;
	m_dir2 = &dir2;
	m_isInverted = isInverted;
}

void TwoPinDcMotor::enable() {
	chibios_rt::CriticalSectionLocker csl;
	if (m_disable) {
		m_disable->setValue(false);
	}

	m_msg = nullptr;
}

void TwoPinDcMotor::disable(const char* msg) {
	chibios_rt::CriticalSectionLocker csl;
	if (m_disable) {
		m_disable->setValue(true);
	}

	m_msg = msg;

	// Also set the duty to zero
	set(0);
}

void TwoPinDcMotor::stop(const char* msg) {
	chibios_rt::CriticalSectionLocker csl;
	m_msg = msg;
	m_value = 0;
	if (m_disable) {
		m_disable->setValue(true);
	}

	// Bypass battery compensation and stop the gate and both direction
	// channels synchronously, including bridges without a disable GPIO.
	if (m_enable) {
		m_enable->setDutyImmediate(0);
	}
	float inactive = m_isInverted ? 1 : 0;
	if (m_dir1) {
		m_dir1->setDutyImmediate(inactive);
	}
	if (m_dir2) {
		m_dir2->setDutyImmediate(inactive);
	}
}

bool TwoPinDcMotor::isOpenDirection() const {
	return m_value >= 0;
}

float TwoPinDcMotor::get() const {
	return m_value;
}

/**
 * @param duty value between -1.0 and 1.0
 */
bool TwoPinDcMotor::set(float duty) {
	chibios_rt::CriticalSectionLocker csl;
	m_value = duty;

	// For low voltage, voltageRatio will be >1 to boost duty so that motor current stays the same
	// At high voltage, the inverse will be true to keep behavior always the same.
	float voltageRatio = 14 / Sensor::get(SensorType::BatteryVoltage).value_or(14);
	duty *= voltageRatio;

	// If not init, don't try to set
	if (!m_dir1 || !m_dir2 || !m_enable) {
		if (m_disable) {
			m_disable->setValue(true);
		}

		return false;
	}

	bool isPositive = duty > 0;

	if (!isPositive) {
		duty = -duty;
	}

	// below here 'duty' is a not negative

	// Clamp to 100%
	if (duty > 1.0f) {
		duty = 1.0f;
	}
	// Disable for very small duty
	else if (duty < 0.01f) {
		duty = 0.0f;
	}

	// If we're in two pin mode, force 100%, else use this pin to PWM
	float enableDuty = m_type == ControlType::PwmEnablePin ? duty : 1;

	// Direction pins get 100% duty unless we're in PwmDirectionPins mode
	float dirDuty = m_type == ControlType::PwmDirectionPins ? duty : 1;

	float recipDuty = 0;
	if (m_isInverted) {
		dirDuty = 1.0f - dirDuty;
		recipDuty = 1.0f;
	}

	m_dir1->setSimplePwmDutyCycle(isPositive ? dirDuty : recipDuty);
	m_dir2->setSimplePwmDutyCycle(isPositive ? recipDuty : dirDuty);
	// Establish direction before reopening a two-wire bridge's gate.
	m_enable->setSimplePwmDutyCycle(enableDuty);

	// This motor has no fault detection, so always return false (indicate success).
	return false;
}
