/*
 * boost_control.cpp
 *
 *  Created on: 13. des. 2019
 *      Author: Ola Ruud
 */
#include "pch.h"

#if EFI_ENGINE_CONTROL

#include "boost_control.h"
#include "electronic_throttle.h"
#include "gppwm_channel.h"

static Map3D<BOOST_RPM_COUNT, BOOST_LOAD_COUNT, uint8_t, int16_t, int16_t> boostMapOpen;
static Map3D<BOOST_RPM_COUNT, BOOST_LOAD_COUNT, uint8_t, int16_t, int16_t> boostMapClosed;
static SimplePwm boostPwmControl("boost");

void BoostController::init(
		IPwm* pwm, const ValueProvider3D* openLoopMap, const ValueProvider3D* closedLoopTargetMap, pid_s* pidParams) {
	m_pwm = pwm;
	m_openLoopMap = openLoopMap;
	m_closedLoopTargetMap = closedLoopTargetMap;

	m_pid.initPidClass(pidParams);
	resetLua();

	m_hasInitBoost = true;
}

void BoostController::resetLua() {
	luaTargetAdd = 0;
	luaTargetMult = 1;
	luaOpenLoopAdd = 0;
}

void BoostController::onConfigurationChange(engine_configuration_s const* previousConfig) {
	if (!previousConfig || !m_pid.isSame(&previousConfig->boostPid)) {
		m_shouldResetPid = true;
	}
}

expected<float> BoostController::observePlant() const {
	return Sensor::get(SensorType::Map);
}

static SensorResult getAxisValue(gppwm_channel_e channel) {
	if (channel == GPPWM_Tps) {
		// Back compat to use pedal if ETB is configured, otherwise real TPS
		return Sensor::get(SensorType::DriverThrottleIntent);
	}

	return readGppwmChannel(channel);
}

expected<float> BoostController::getSetpoint() {
	m_lastTarget = getSetpointImpl();
	boostControlTarget = m_lastTarget.value_or(0);
	return m_lastTarget;
}

expected<float> BoostController::getSetpointImpl() {
	// If we're in open loop only mode, disregard any target computation.
	// Open loop needs to work even in case of invalid closed loop config
	if (engineConfiguration->boostType != CLOSED_LOOP) {
		return 0;
	}

	auto xAxis = getAxisValue(engineConfiguration->boostClosedLoopXAxis);
	if (!xAxis) {
		return unexpected;
	}

	auto yAxis = getAxisValue(engineConfiguration->boostClosedLoopYAxis);
	if (!yAxis) {
		return unexpected;
	}

	boostClosedLoopXAxisValue = xAxis.Value;
	boostClosedLoopYAxisValue = yAxis.Value;

	efiAssert(
			ObdCode::OBD_PCM_Processor_Fault, m_closedLoopTargetMap != nullptr, "boost closed loop target", unexpected);

	float target = m_closedLoopTargetMap->getValue(xAxis.Value, yAxis.Value);

	// Add any blends if configured
	for (size_t i = 0; i < efi::size(config->boostClosedLoopBlends); i++) {
		auto result = calculateBlend(config->boostClosedLoopBlends[i], xAxis.Value, yAxis.Value);

		boostClosedLoopBlendParameter[i] = result.BlendParameter;
		boostClosedLoopBlendBias[i] = result.Bias;
		boostClosedLoopBlendOutput[i] = result.Value;
		boostClosedLoopBlendYAxis[i] = result.TableYAxis;

		target += result.Value;
	}

	return target * luaTargetMult + luaTargetAdd;
}

expected<percent_t> BoostController::getOpenLoop(float target) {
	// Boost control open loop doesn't care about target - only TPS/RPM
	UNUSED(target);

	auto xAxis = getAxisValue(engineConfiguration->boostOpenLoopXAxis);
	if (!xAxis) {
		return unexpected;
	}

	auto yAxis = getAxisValue(engineConfiguration->boostOpenLoopYAxis);
	if (!yAxis) {
		return unexpected;
	}

	boostOpenLoopXAxisValue = xAxis.Value;
	boostOpenLoopYAxisValue = yAxis.Value;

	efiAssert(ObdCode::OBD_PCM_Processor_Fault, m_openLoopMap != nullptr, "boost open loop", unexpected);

	float openLoop = luaOpenLoopAdd + m_openLoopMap->getValue(xAxis.Value, yAxis.Value);

	// Add any blends if configured
	for (size_t i = 0; i < efi::size(config->boostOpenLoopBlends); i++) {
		auto result = calculateBlend(config->boostOpenLoopBlends[i], xAxis.Value, yAxis.Value);

		boostOpenLoopBlendParameter[i] = result.BlendParameter;
		boostOpenLoopBlendBias[i] = result.Bias;
		boostOpenLoopBlendOutput[i] = result.Value;
		boostOpenLoopBlendYAxis[i] = result.TableYAxis;

		openLoop += result.Value;
	}

	openLoop = std::clamp<float>(openLoop, 0, 100);

	openLoopPart = openLoop;
	return openLoop;
}

percent_t BoostController::getClosedLoopImpl(float target, float manifoldPressure) {
	// If we're in open loop only mode, make no closed loop correction.
	if (engineConfiguration->boostType != CLOSED_LOOP) {
		return 0;
	}

	// Reset PID if requested
	if (m_shouldResetPid) {
		m_pid.reset();
		m_shouldResetPid = false;
	}

	// If the engine isn't running, don't correct.
	isZeroRpm = Sensor::getOrZero(SensorType::Rpm) == 0;
	if (isZeroRpm) {
		m_pid.reset();
		return 0;
	}

	isBelowClosedLoopThreshold = manifoldPressure < engineConfiguration->minimumBoostClosedLoopMap;
	if (isBelowClosedLoopThreshold) {
		// We're below the CL threshold, inhibit CL for now
		m_pid.reset();
		return 0;
	}

	return m_pid.getOutput(target, manifoldPressure, FAST_CALLBACK_PERIOD_MS / 1000.0f);
}

expected<percent_t> BoostController::getClosedLoop(float target, float manifoldPressure) {
	boostControllerClosedLoopPart = getClosedLoopImpl(target, manifoldPressure);

	m_pid.postState(engine->outputChannels.boostStatus);

	return (float)boostControllerClosedLoopPart;
}

void BoostController::setOutput(expected<float> output) {
	boostOutput = output.value_or(engineConfiguration->boostControlSafeDutyCycle);

	if (!engineConfiguration->isBoostControlEnabled) {
		// If not enabled, force 0% output
		boostOutput = 0;
	}

	float duty = PERCENT_TO_DUTY(boostOutput);

	if (m_pwm) {
		m_pwm->setSimplePwmDutyCycle(duty);
	}

	// inject wastegate position into DC controllers, pretty weird workflow to be honest
	// todo: should it be DC controller pulling?
	setEtbWastegatePosition(boostOutput);
}

void BoostController::onFastCallback() {
	if (!m_hasInitBoost) {
		return;
	}

	m_pid.iTermMin = -20;
	m_pid.iTermMax = 20;

	rpmTooLow = Sensor::getOrZero(SensorType::Rpm) <= engineConfiguration->boostControlMinRpm;
	tpsTooLow = Sensor::getOrZero(SensorType::Tps1) < engineConfiguration->boostControlMinTps;
	mapTooLow = Sensor::getOrZero(SensorType::Map) < engineConfiguration->boostControlMinMap;

	if (rpmTooLow || tpsTooLow || mapTooLow) {
		// Keep the target current for anything that reads it while we're inactive
		getSetpoint();

		// Passing unexpected will use the safe duty cycle configured by the user
		setOutput(unexpected);
	} else {
		ClosedLoopController::update();
	}
}

void setDefaultBoostParameters() {
	engineConfiguration->boostPwmFrequency = 33;
	engineConfiguration->boostPid.offset = 0;
	engineConfiguration->boostPid.pFactor = 0.5;
	engineConfiguration->boostPid.iFactor = 0.3;
	engineConfiguration->boostPid.maxValue = 20;
	engineConfiguration->boostPid.minValue = -20;
	engineConfiguration->boostOpenLoopXAxis = GPPWM_Rpm;
	engineConfiguration->boostOpenLoopYAxis = GPPWM_Tps;
	engineConfiguration->boostClosedLoopXAxis = GPPWM_Rpm;
	engineConfiguration->boostClosedLoopYAxis = GPPWM_Tps;

	setLinearCurve(config->boostRpmBins, 0, 8000, 1);
	setLinearCurve(config->boostTpsBins, 0, 100, 1);
	setLinearCurve(config->boostClosedLoopXAxisBins, 0, 8000, 1);
	setLinearCurve(config->boostClosedLoopYAxisBins, 0, 100, 1);

	for (int loadIndex = 0; loadIndex < BOOST_LOAD_COUNT; loadIndex++) {
		for (int rpmIndex = 0; rpmIndex < BOOST_RPM_COUNT; rpmIndex++) {
			config->boostTableOpenLoop[loadIndex][rpmIndex] = (float)config->boostTpsBins[loadIndex];
			config->boostTableClosedLoop[loadIndex][rpmIndex] = (float)config->boostTpsBins[loadIndex];
		}
	}

	// Defaults for ETB-style wastegate actuator
	engineConfiguration->etbWastegatePid.pFactor = 1;
	engineConfiguration->etbWastegatePid.minValue = -60;
	engineConfiguration->etbWastegatePid.maxValue = 60;
}

static ConfiguredPwm boostOutput(boostPwmControl);

static bool isBoostPwmWanted() {
	return engineConfiguration->isBoostControlEnabled && isBrainPinValid(engineConfiguration->boostControlPin) &&
		   engineConfiguration->boostPwmFrequency >= 1;
}

static bool hasEtbWastegate() {
	for (size_t i = 0; i < efi::size(engineConfiguration->etbFunctions); i++) {
		if (engineConfiguration->etbFunctions[i] == DC_Wastegate) {
			return true;
		}
	}

	return false;
}

void stopBoostControl(const engine_configuration_s* /*prev*/) {
	boostOutput.stopIfUnwanted(isBoostPwmWanted());
}

void startBoostControl(const engine_configuration_s* prev) {
	float frequency = engineConfiguration->boostPwmFrequency;

	boostOutput.startIfWanted(prev, isBoostPwmWanted(), "Boost", &enginePins.boostPin, frequency, 0);

	if (boostOutput.isRunning()) {
		// Already running: follow a frequency change
		boostPwmControl.setFrequency(frequency);
	}

	// todo: why do we have 'isBoostControlEnabled' setting exactly?
	// 'initVvtActuators' is an example of a subsystem without explicit enable
	bool controllerWanted = engineConfiguration->isBoostControlEnabled &&
							(isBrainPinValid(engineConfiguration->boostControlPin) || hasEtbWastegate());

	auto& controller = engine->module<BoostController>().unmock();

	// The first time it's wanted. What it's given never changes, and it reads the enable on every update
	// (zero output when off), so after that there's nothing to redo.
	if (controllerWanted && !controller.hasInit()) {
		// Set up open & closed loop tables
		boostMapOpen.init(config->boostTableOpenLoop, config->boostTpsBins, config->boostRpmBins);
		boostMapClosed.init(
				config->boostTableClosedLoop, config->boostClosedLoopYAxisBins, config->boostClosedLoopXAxisBins);

		controller.init(&boostPwmControl, &boostMapOpen, &boostMapClosed, &engineConfiguration->boostPid);
	}
}

#endif // EFI_ENGINE_CONTROL
