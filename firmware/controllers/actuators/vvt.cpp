/*
 * @file vvt.cpp
 *
 * @date Jun 26, 2016
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"

#include "local_version_holder.h"
#include "vvt.h"
#include "gppwm_channel.h"

#define NO_PIN_PERIOD 500

using vvt_map_t = Map3D<SCRIPT_TABLE_8, SCRIPT_TABLE_8, int8_t, uint16_t, uint16_t>;

static vvt_map_t vvtTable1;
static vvt_map_t vvtTable2;

VvtController::VvtController(int index, int bankIndex, int camIndex)
	: m_index(index)
	, m_bank(bankIndex)
	, m_cam(camIndex)
{
}

void VvtController::init(const ValueProvider3D* targetMap, IPwm* pwm) {
	// Use the same settings for the Nth cam in every bank (ie, all exhaust cams use the same PID)
	m_pid.initPidClass(&engineConfiguration->auxPid[m_cam]);
	
	m_pid.iTermMin = engineConfiguration->vvtItermMin[m_cam];
	m_pid.iTermMax = engineConfiguration->vvtItermMax[m_cam];

	m_targetMap = targetMap;
	m_pwm = pwm;
}

void VvtController::onFastCallback() {
	if (!m_pwm || !m_targetMap) {
		// not init yet
		return;
	}

	m_isRpmHighEnough = Sensor::getOrZero(SensorType::Rpm) > engineConfiguration->vvtControlMinRpm;
	m_isCltWarmEnough = Sensor::getOrZero(SensorType::Clt) > engineConfiguration->vvtControlMinClt;

	auto nowNt = getTimeNowNt();
	m_engineRunningLongEnough = engine->rpmCalculator.getSecondsSinceEngineStart(nowNt) > engineConfiguration->vvtActivationDelayMs / MS_PER_SECOND;
	if (!m_engineRunningLongEnough) {
		m_timeSinceEnabled.reset();
	}

	update();
}

void VvtController::onConfigurationChange(engine_configuration_s const * previousConfig) {
	m_pid.iTermMin = engineConfiguration->vvtItermMin[m_cam];
	m_pid.iTermMax = engineConfiguration->vvtItermMax[m_cam];

	if (!previousConfig || !m_pid.isSame(&previousConfig->auxPid[m_cam])) {
		m_pid.reset();
	}
}

static bool shouldInvertVvt(int camIndex) {
	// grumble grumble, can't do an array of bits in c++
	switch (camIndex) {
		case 0: return engineConfiguration->invertVvtControlIntake;
		case 1: return engineConfiguration->invertVvtControlExhaust;
	}

	return false;
}

expected<angle_t> VvtController::observePlant() const {
#if EFI_SHAFT_POSITION_INPUT
	return engine->triggerCentral.getVVTPosition(m_bank, m_cam);
#else
	return unexpected;
#endif // EFI_SHAFT_POSITION_INPUT
}

expected<angle_t> VvtController::getSetpoint() {
	float rpm = Sensor::getOrZero(SensorType::Rpm);
	float load = getFuelingLoad();

	auto yAxisOverride =
		(m_cam == 0)
		? engineConfiguration->vvtIntakeYAxisOverride
		: engineConfiguration->vvtExhaustYAxisOverride;

	if (yAxisOverride != GPPWM_Zero) {
		load = readGppwmChannel(yAxisOverride).value_or(0);
	}

	targetYAxis = load;

	float target = m_targetMap->getValue(rpm, load);

	if (!m_targetOffsetTimer.hasElapsedSec(2)) {
		target += m_targetOffset;
	}

	// Ramp the target in over 2 seconds once we're allowed to control VVT
	target = interpolateClamped(
				0, 0,
				2, target,
				m_timeSinceEnabled.getElapsedSeconds()
			);

	// If the target is very near the rest position, disable control entirely
	// Couple reasons for this:
	// - Avoid integrator windup from trying to jam the cam against the stop
	// - Many VVT implementations don't like being controlled near the stop,
	//       as this can cause problems with the lock pin jamming.
	bool allowCamControl;
	if (shouldInvertVvt(m_cam)) {
		allowCamControl = m_targetHysteresis.test(target < -3, target > -1);
	} else {
		allowCamControl = m_targetHysteresis.test(target > 3, target < 1);
	}

	if (allowCamControl) {
		vvtTarget = target;
		return target;
	} else {
		vvtTarget = 0;
		return unexpected;
	}
}

void VvtController::setTargetOffset(float targetOffset) {
	m_targetOffset = targetOffset;
	m_targetOffsetTimer.reset();
}

expected<percent_t> VvtController::getOpenLoop(angle_t /* target */) {
	const auto& bins = config->vvtOpenLoop[m_cam].bins;
	const auto& values = config->vvtOpenLoop[m_cam].values;

	// Oil temp if we have it
	// Coolant temp if we don't
	// And if it's dead, default to 80C
	float temp;
	auto oilT = Sensor::get(SensorType::OilTemperature);
	if (oilT) {
		temp = oilT.Value;
	} else {
		temp = Sensor::get(SensorType::Clt).value_or(80);
	}

	return interpolate2d(temp, bins, values);
}

expected<percent_t> VvtController::getClosedLoop(angle_t target, angle_t observation) {
	// User labels say "advance" and "retard"
	// "advance" means that additional solenoid duty makes indicated VVT position more positive
	// "retard" means that additional solenoid duty makes indicated VVT position more negative
	bool isInverted = shouldInvertVvt(m_cam);
	m_pid.setErrorAmplification(isInverted ? -1.0f : 1.0f);

	float retVal = m_pid.getOutput(target, observation, FAST_CALLBACK_PERIOD_MS / 1000.0f);

	m_pid.postState(*reinterpret_cast<pid_status_s*>(&pidState));

	return retVal;
}

void VvtController::setOutput(expected<percent_t> outputValue) {
#if EFI_SHAFT_POSITION_INPUT
	bool enabled =
		m_engineRunningLongEnough &&
		m_isRpmHighEnough &&
		m_isCltWarmEnough;

	if (outputValue && enabled) {
		float vvtPct = outputValue.Value;

		// Compensate for battery voltage so that the % output is actually % solenoid current normalized
		// to a 14v supply (boost duty when battery is low, etc)
		float voltageRatio = 14 / clampF(10, Sensor::get(SensorType::BatteryVoltage).value_or(14), 24);
		vvtPct *= voltageRatio;

		// Clamp final output min/max
		vvtPct = clampF(
			engineConfiguration->vvtOutputMin[m_cam],
			vvtPct,
			engineConfiguration->vvtOutputMax[m_cam]
		);

		vvtOutput = vvtPct;

		m_pwm->setSimplePwmDutyCycle(PERCENT_TO_DUTY(vvtPct));
	} else {
		m_pwm->setSimplePwmDutyCycle(0);

		vvtOutput = 0;

		// we need to avoid accumulating iTerm while engine is not running
		m_pid.reset();
	}
#endif // EFI_SHAFT_POSITION_INPUT
}

#if EFI_VVT_PID

static const char *vvtOutputNames[CAM_INPUTS_COUNT] = {
"Vvt Output#1",
#if CAM_INPUTS_COUNT > 1
"Vvt Output#2",
#endif
#if CAM_INPUTS_COUNT > 2
"Vvt Output#3",
#endif
#if CAM_INPUTS_COUNT > 3
"Vvt Output#4",
#endif
 };

static OutputPin vvtPins[CAM_INPUTS_COUNT];
static SimplePwm vvtPwms[CAM_INPUTS_COUNT] = { "VVT1", "VVT2", "VVT3", "VVT4" };

static void turnVvtPidOn(int index) {
	if (!isBrainPinValid(engineConfiguration->vvtPins[index])) {
		return;
	}

	startSimplePwmExt(&vvtPwms[index], vvtOutputNames[index],
			engineConfiguration->vvtPins[index],
			&vvtPins[index],
			engineConfiguration->vvtOutputFrequency, 0);
}

void startVvtControlPins() {
	for (int i = 0; i <CAM_INPUTS_COUNT; i++) {
		turnVvtPidOn(i);
	}
}

void stopVvtControlPins() {
	for (int i = 0; i < CAM_INPUTS_COUNT; i++) {
		vvtPins[i].deInit();
	}
}

void initVvtActuators() {
	if (engineConfiguration->vvtControlMinRpm < engineConfiguration->cranking.rpm) {
		engineConfiguration->vvtControlMinRpm = engineConfiguration->cranking.rpm;
	}

	vvtTable1.init(config->vvtTable1, config->vvtTable1LoadBins,
			config->vvtTable1RpmBins);
	vvtTable2.init(config->vvtTable2, config->vvtTable2LoadBins,
			config->vvtTable2RpmBins);


	engine->module<VvtController1>()->init(&vvtTable1, &vvtPwms[0]);
	engine->module<VvtController2>()->init(&vvtTable2, &vvtPwms[1]);
	engine->module<VvtController3>()->init(&vvtTable1, &vvtPwms[2]);
	engine->module<VvtController4>()->init(&vvtTable2, &vvtPwms[3]);

	startVvtControlPins();
}

#endif
