#include "pch.h"

#include "fan_control.h"
#include "bench_test.h"
#include "gppwm_channel.h"

#if !EFI_UNIT_TEST
static SimplePwm fan1Pwm("Fan1");
static SimplePwm fan2Pwm("Fan2");
#endif

// Fan 1 duty lookup implementations
float FanControl1::getAcOffDuty(float clt) {
	auto xAxis = readGppwmChannel(engineConfiguration->fan1PwmXAxis);
	if (!xAxis) {
		return engineConfiguration->fanPwmSafetyDuty;
	}
	fanXAxisValue = xAxis.Value;
	return interpolate3d(
		config->fan1DutyAcOff,
		config->fan1CltBins, clt,
		config->fan1XAxisBins, xAxis.Value
	);
}

float FanControl1::getAcOnDuty(float clt) {
	auto xAxis = readGppwmChannel(engineConfiguration->fan1PwmXAxis);
	if (!xAxis) {
		return engineConfiguration->fanPwmSafetyDuty;
	}
	fanXAxisValue = xAxis.Value;
	return interpolate3d(
		config->fan1DutyAcOn,
		config->fan1CltBins, clt,
		config->fan1XAxisBins, xAxis.Value
	);
}

// Fan 2 duty lookup implementations
float FanControl2::getAcOffDuty(float clt) {
	auto xAxis = readGppwmChannel(engineConfiguration->fan2PwmXAxis);
	if (!xAxis) {
		return engineConfiguration->fanPwmSafetyDuty;
	}
	fanXAxisValue = xAxis.Value;
	return interpolate3d(
		config->fan2DutyAcOff,
		config->fan2CltBins, clt,
		config->fan2XAxisBins, xAxis.Value
	);
}

float FanControl2::getAcOnDuty(float clt) {
	auto xAxis = readGppwmChannel(engineConfiguration->fan2PwmXAxis);
	if (!xAxis) {
		return engineConfiguration->fanPwmSafetyDuty;
	}
	fanXAxisValue = xAxis.Value;
	return interpolate3d(
		config->fan2DutyAcOn,
		config->fan2CltBins, clt,
		config->fan2XAxisBins, xAxis.Value
	);
}

bool FanController::getState(bool acActive, bool lastState) {
	auto clt = Sensor::get(SensorType::Clt);

#if EFI_SHAFT_POSITION_INPUT
	bool cranking = engine->rpmCalculator.isCranking();
	bool notRunning = !engine->rpmCalculator.isRunning();
#else
	bool cranking = false;
	bool notRunning = true;
#endif

	disabledWhileEngineStopped = notRunning && disableWhenStopped();
	brokenClt = !clt;
	enabledForAc = enableWithAc() && acActive;
	hot = clt.value_or(0) > getFanOnTemp();
	cold = clt.value_or(0) < getFanOffTemp();

	if (!m_benchTestTimer.hasElapsedSec(3)) {
		// Run the fan when bench test is active
		return true;
	} else if (!m_ignitionState) {
		// Inhibit while ignition is off
		return false;
	} else if (cranking) {
		// Inhibit while cranking
		return false;
	} else if (disabledWhileEngineStopped) {
		// Inhibit while not running (if so configured)
		return false;
	} else if (brokenClt) {
		// If CLT is broken, turn the fan on
		return true;
	} else if (enabledForAc) {
		return true;
	} else if (hot) {
		// If hot, turn the fan on
		return true;
	} else if (cold) {
		// If cold, turn the fan off
		return false;
	} else {
		// no condition met, maintain previous state
		return lastState;
	}
}

float FanController::getPwmDuty(bool acActive) {
	auto clt = Sensor::get(SensorType::Clt);
	brokenClt = !clt;

	if (brokenClt) {
		return engineConfiguration->fanPwmSafetyDuty;
	}

	return acActive ? getAcOnDuty(clt.Value) : getAcOffDuty(clt.Value);
}

void FanController::onSlowCallback() {
#if EFI_PROD_CODE
	if (isRunningBenchTest()) {
		return; // let's not mess with bench testing
	}
#endif

	bool acActive = engine->module<AcController>()->isAcEnabled();

	auto& pin = getPin();

	bool result = getState(acActive, pin.getLogicValue());
	m_state = result;

	if (usePwmMode() && m_pwm) {
		// PWM mode: use on/off logic to determine if fan should run,
		// then use PWM table to determine speed when on

		float duty;
		if (!result) {
			duty = 0.0f;
		} else {
			duty = getPwmDuty(acActive);
		}

		fanDuty = duty;
		m_state = duty > 0;
		m_pwm->setSimplePwmDutyCycle(PERCENT_TO_DUTY(duty));
	} else {
		// On/off mode
		pin.setValue(result);
		
	}
}

void FanController::onIgnitionStateChanged(bool ignitionOn) {
	m_ignitionState = ignitionOn;
}

void FanController::benchTest() {
	m_benchTestTimer.reset();
}

void initFanControl() {
#if !EFI_UNIT_TEST
	// Fan 1 PWM
	if (engineConfiguration->fan1UsePwmMode && isBrainPinValid(engineConfiguration->fanPin)) {
		startSimplePwm(&fan1Pwm, "Fan1", &enginePins.fanRelay,
		               engineConfiguration->fanPwmFrequency, 0);
		engine->module<FanControl1>().unmock().m_pwm = &fan1Pwm;
	}

	// Fan 2 PWM
	if (engineConfiguration->fan2UsePwmMode && isBrainPinValid(engineConfiguration->fan2Pin)) {
		startSimplePwm(&fan2Pwm, "Fan2", &enginePins.fanRelay2,
		               engineConfiguration->fanPwmFrequency, 0);
		engine->module<FanControl2>().unmock().m_pwm = &fan2Pwm;
	}
#endif
}
