/**
 * @file    idle_thread.cpp
 * @brief   Idle Air Control valve thread.
 *
 * This thread looks at current RPM and decides if it should increase or decrease IAC duty cycle.
 * This file has the hardware & scheduling logic, desired idle level lives separately.
 *
 *
 * @date May 23, 2013
 * @author Andrey Belomutskiy, (c) 2012-2022
 */

#include "pch.h"

#if EFI_IDLE_CONTROL
#include "idle_thread.h"
#include "idle_hardware.h"

#include "dc_motors.h"

#if EFI_TUNER_STUDIO
#include "stepper.h"
#endif

IIdleController::TargetInfo IdleController::getTargetRpm(float clt) {
	// Base target RPM from CLT table
	targetRpmByClt = interpolate2d(clt, config->cltIdleRpmBins, config->cltIdleRpm);

	// idle air Bump for AC
	// Why do we bump based on button not based on actual A/C relay state?
	// Because AC output has a delay to allow idle bump to happen first, so that the airflow increase gets a head start on the load increase
	// alternator duty cycle has a similar logic
	targetRpmAcBump = engine->module<AcController>().unmock().acButtonState ? engineConfiguration->acIdleRpmBump : 0;

	auto target = targetRpmByClt + targetRpmAcBump + luaAddRpm;

	float rpmUpperLimit = engineConfiguration->idlePidRpmUpperLimit;
	float entryRpm = target + rpmUpperLimit;

	// Higher exit than entry to add some hysteresis to avoid bouncing around upper threshold
	float exitRpm = target + 1.5 * rpmUpperLimit;

	if (engineConfiguration->idleReturnTargetRamp) {
		// Ramp the target down from the transition RPM to normal over a few seconds
		float timeSinceIdleEntry = m_timeInIdlePhase.getElapsedSeconds();
		target += interpolateClamped(
			0, rpmUpperLimit,
			3, 0,
			timeSinceIdleEntry
		);
	}

	idleTarget = target;
	return { target, entryRpm, exitRpm };
}

IIdleController::Phase IdleController::determinePhase(float rpm, IIdleController::TargetInfo targetRpm, SensorResult tps, float vss, float crankingTaperFraction) {
#if EFI_SHAFT_POSITION_INPUT
	if (!engine->rpmCalculator.isRunning()) {
		return Phase::Cranking;
	}

	if (!tps) {
		// If the TPS has failed, assume the engine is running
		return Phase::Running;
	}

	// if throttle pressed, we're out of the idle corner
	if (tps.Value > engineConfiguration->idlePidDeactivationTpsThreshold) {
		return Phase::Running;
	}

	// If rpm too high (but throttle not pressed), we're coasting
	// ALSO, if still in the cranking taper, disable coasting
	if (rpm > targetRpm.IdleExitRpm) {
		looksLikeCoasting = true;
	} else if (rpm < targetRpm.IdleEntryRpm) {
		looksLikeCoasting = false;
	}

	looksLikeCrankToIdle = crankingTaperFraction < 1;
	if (looksLikeCoasting && !looksLikeCrankToIdle) {
		return Phase::Coasting;
	}

	// If the vehicle is moving too quickly, disable CL idle
	auto maxVss = engineConfiguration->maxIdleVss;
	looksLikeRunning = maxVss != 0 && vss > maxVss;
	if (looksLikeRunning) {
		return Phase::Running;
	}

	// If still in the cranking taper, disable closed loop idle
	if (looksLikeCrankToIdle) {
		return Phase::CrankToIdleTaper;
	}
#endif // EFI_SHAFT_POSITION_INPUT

	// No other conditions met, we are idling!
	return Phase::Idling;
}

float IdleController::getCrankingTaperFraction(float clt) const {
	float taperDuration = engineConfiguration->afterCrankingIACtaperDuration;

	if (engineConfiguration->useCrankingIdleTaperTableSetting) {
		taperDuration *= interpolate2d(clt, config->cltCrankingTaperCorrBins, config->cltCrankingTaperCorr);
	}

	return (float)engine->rpmCalculator.getRevolutionCounterSinceStart() / taperDuration;
}

float IdleController::getCrankingOpenLoop(float clt) const {
	float mult =
		engineConfiguration->overrideCrankingIacSetting
		// Override to separate table
	 	? interpolate2d(clt, config->cltCrankingCorrBins, config->cltCrankingCorr)
		// Otherwise use plain running table
		: interpolate2d(clt, config->cltIdleCorrBins, config->cltIdleCorr);

	return engineConfiguration->crankingIACposition * mult;
}

percent_t IdleController::getRunningOpenLoop(float rpm, float clt, SensorResult tps) {
	float running =
		engineConfiguration->manIdlePosition		// Base idle position (slider)
		* interpolate2d(clt, config->cltIdleCorrBins, config->cltIdleCorr);
	openLoopBase = running;

	// Now we bump it by the AC/fan amount if necessary
	openLoopAcBump = engine->module<AcController>().unmock().acButtonState ? engineConfiguration->acIdleExtraOffset : 0;
	openLoopFanBump =
		  (enginePins.fanRelay.getLogicValue()  ? engineConfiguration->fan1ExtraIdle : 0)
		+ (enginePins.fanRelay2.getLogicValue() ? engineConfiguration->fan2ExtraIdle : 0);

	running += openLoopAcBump;
	running += openLoopFanBump;
	running += luaAdd;

#if EFI_ANTILAG_SYSTEM 
if (engine->antilagController.isAntilagCondition) {
	running += engineConfiguration->ALSIdleAdd;
}
#endif /* EFI_ANTILAG_SYSTEM */

	// Now bump it by the specified amount when the throttle is opened (if configured)
	// nb: invalid tps will make no change, no explicit check required
	iacByTpsTaper = interpolateClamped(
		0, 0,
		engineConfiguration->idlePidDeactivationTpsThreshold, engineConfiguration->iacByTpsTaper,
		tps.value_or(0));

	running += iacByTpsTaper;

	float airTaperRpmUpperLimit = engineConfiguration->idlePidRpmUpperLimit + engineConfiguration->airTaperRpmRange;
	iacByRpmTaper = interpolateClamped(
		engineConfiguration->idlePidRpmUpperLimit, 0,
		airTaperRpmUpperLimit, engineConfiguration->airByRpmTaper,
		rpm);

	running += iacByRpmTaper;

	return clampF(0, running, 100);
}

percent_t IdleController::getOpenLoop(Phase phase, float rpm, float clt, SensorResult tps, float crankingTaperFraction) {
	percent_t crankingValvePosition = getCrankingOpenLoop(clt);

	isCranking = phase == Phase::Cranking;
	isIdleCoasting = phase == Phase::Coasting || (phase == Phase::Running && engineConfiguration->modeledFlowIdle);

	// if we're cranking, nothing more to do.
	if (isCranking) {
		return crankingValvePosition;
	}

	// If coasting (and enabled), use the coasting position table instead of normal open loop
	isIacTableForCoasting = engineConfiguration->useIacTableForCoasting && isIdleCoasting;
	if (isIacTableForCoasting) {
		return interpolate2d(rpm, config->iacCoastingRpmBins, config->iacCoasting);
	}

	percent_t running = getRunningOpenLoop(rpm, clt, tps);

	// Interpolate between cranking and running over a short time
	// This clamps once you fall off the end, so no explicit check for >1 required
	return interpolateClamped(0, crankingValvePosition, 1, running, crankingTaperFraction);
}

float IdleController::getIdleTimingAdjustment(float rpm) {
	return getIdleTimingAdjustment(rpm, m_lastTargetRpm, m_lastPhase);
}

float IdleController::getIdleTimingAdjustment(float rpm, float targetRpm, Phase phase) {
	// if not enabled, do nothing
	if (!engineConfiguration->useIdleTimingPidControl) {
		return 0;
	}

	// If not idling, do nothing
	if (phase != Phase::Idling) {
		m_timingPid.reset();
		return 0;
	}

	if (engineConfiguration->modeledFlowIdle) {
		return m_modeledFlowIdleTiming;
	} else {
		// We're now in the idle mode, and RPM is inside the Timing-PID regulator work zone!
		return m_timingPid.getOutput(targetRpm, rpm, FAST_CALLBACK_PERIOD_MS / 1000.0f);
	}
}

static void finishIdleTestIfNeeded() {
	if (engine->timeToStopIdleTest != 0 && getTimeNowUs() > engine->timeToStopIdleTest)
		engine->timeToStopIdleTest = 0;
}

/**
 * @return idle valve position percentage for automatic closed loop mode
 */
float IdleController::getClosedLoop(IIdleController::Phase phase, float tpsPos, float rpm, float targetRpm) {
	if (shouldResetPid) {
		needReset = m_pid.getIntegration() <= 0 || mustResetPid;
		// we reset only if I-term is negative, because the positive I-term is good - it keeps RPM from dropping too low
		if (needReset) {
			m_pid.reset();
			mustResetPid = false;
		}
		shouldResetPid = false;
	}

	notIdling = phase != IIdleController::Phase::Idling;
	if (notIdling) {
		// Don't store old I and D terms if PID doesn't work anymore.
		// Otherwise they will affect the idle position much later, when the throttle is closed.
		if (mightResetPid) {
			mightResetPid = false;
			shouldResetPid = true;
		}

		// We aren't idling, so don't apply any correction.  A positive correction could inhibit a return to idle.
		m_lastAutomaticPosition = 0;
		return 0;
	}

	// #1553 we need to give FSIO variable offset or minValue a chance
	bool acToggleJustTouched = engine->module<AcController>().unmock().timeSinceStateChange.getElapsedSeconds() < 0.5f /*second*/;
	// check if within the dead zone
	isInDeadZone = !acToggleJustTouched && std::abs(rpm - targetRpm) <= engineConfiguration->idlePidRpmDeadZone;
	if (isInDeadZone) {
		// current RPM is close enough, no need to change anything
		return m_lastAutomaticPosition;
	}

	percent_t newValue = m_pid.getOutput(targetRpm, rpm, FAST_CALLBACK_PERIOD_MS / 1000.0f);

	// the state of PID has been changed, so we might reset it now, but only when needed (see idlePidDeactivationTpsThreshold)
	mightResetPid = true;

	// Apply PID Deactivation Threshold as a smooth taper for TPS transients.
	// if tps==0 then PID just works as usual, or we completely disable it if tps>=threshold
	// TODO: should we just remove this? It reduces the gain if your zero throttle stop isn't perfect,
	// which could give unstable results.
	newValue = interpolateClamped(0, newValue, engineConfiguration->idlePidDeactivationTpsThreshold, 0, tpsPos);

	m_lastAutomaticPosition = newValue;
	return newValue;
}

float IdleController::getIdlePosition(float rpm) {
#if EFI_SHAFT_POSITION_INPUT
	// Simplify hardware CI: we borrow the idle valve controller as a PWM source for various stimulation tasks
	// The logic in this function is solidly unit tested, so it's not necessary to re-test the particulars on real hardware.
	#ifdef HARDWARE_CI
		return engineConfiguration->manIdlePosition;
	#endif

	bool useModeledFlow = engineConfiguration->modeledFlowIdle;

	/*
	* Here we have idle logic thread - actual stepper movement is implemented in a separate
	* working thread see stepper.cpp
	*/
	m_pid.iTermMin = engineConfiguration->idlerpmpid_iTermMin;
	m_pid.iTermMax = engineConfiguration->idlerpmpid_iTermMax;

	// On failed sensor, use 0 deg C - should give a safe highish idle
	float clt = Sensor::getOrZero(SensorType::Clt);
	auto tps = Sensor::get(SensorType::DriverThrottleIntent);

	// Compute the target we're shooting for
	auto targetRpm = getTargetRpm(clt);
	m_lastTargetRpm = targetRpm.ClosedLoopTarget;

	// Determine cranking taper (modeled flow does no taper of open loop)
	float crankingTaper = useModeledFlow ? 1 : getCrankingTaperFraction(clt);

	// Determine what operation phase we're in - idling or not
	float vehicleSpeed = Sensor::getOrZero(SensorType::VehicleSpeed);
	auto phase = determinePhase(rpm, targetRpm, tps, vehicleSpeed, crankingTaper);

	if (phase != m_lastPhase && phase == Phase::Idling) {
		// Just entered idle, reset timer
		m_timeInIdlePhase.reset();
	}

	m_lastPhase = phase;

	finishIdleTestIfNeeded();

	// Always apply open loop correction
	percent_t iacPosition = getOpenLoop(phase, rpm, clt, tps, crankingTaper);
	openLoop = iacPosition;

	// Force closed loop operation for modeled flow
	auto idleMode = useModeledFlow ? IM_AUTO : engineConfiguration->idleMode;

	// If TPS is working and automatic mode enabled, add any closed loop correction
	if (tps.Valid && idleMode == IM_AUTO) {
		if (useModeledFlow && phase != Phase::Idling) {
			m_pid.reset();
		}

		auto closedLoop = getClosedLoop(phase, tps.Value, rpm, targetRpm.ClosedLoopTarget);
		idleClosedLoop = closedLoop;
		iacPosition += closedLoop;
	} else {
		idleClosedLoop = 0;
	}

	iacPosition = clampPercentValue(iacPosition);

#if EFI_TUNER_STUDIO && (EFI_PROD_CODE || EFI_SIMULATOR)
	if (useModeledFlow || idleMode == IM_AUTO) {
		// see also tsOutputChannels->idlePosition
		m_pid.postState(engine->outputChannels.idleStatus);
	}

	extern StepperMotor iacMotor;
	engine->outputChannels.idleStepperTargetPosition = iacMotor.getTargetPosition();
#endif /* EFI_TUNER_STUDIO */

	if (useModeledFlow && phase != Phase::Cranking) {
		float totalAirmass = 0.01 * iacPosition * engineConfiguration->idleMaximumAirmass;
		idleTargetAirmass = totalAirmass;

		bool shouldAdjustTiming = engineConfiguration->useIdleTimingPidControl && phase == Phase::Idling;

		// extract hiqh frequency content to be handled by timing
		float timingAirmass = shouldAdjustTiming ? m_timingHpf.filter(totalAirmass) : 0;

		// Convert from airmass delta -> timing
		m_modeledFlowIdleTiming = interpolate2d(timingAirmass, config->airmassToTimingBins, config->airmassToTimingValues);

		// Handle the residual low frequency content with airflow
		float idleAirmass = totalAirmass - timingAirmass;
		float airflowKgPerH = 3.6 * 0.001 * idleAirmass * rpm / 60 * engineConfiguration->cylindersCount / 2;
		idleTargetFlow = airflowKgPerH;

		// Convert from desired flow -> idle valve position
		float idlePos = interpolate2d(
			airflowKgPerH,
			config->idleFlowEstimateFlow,
			config->idleFlowEstimatePosition
		);

		iacPosition = idlePos;
	}

	currentIdlePosition = iacPosition;
	isIdleClosedLoop = phase == Phase::Idling;
	return iacPosition;
#else
	return 0;
#endif // EFI_SHAFT_POSITION_INPUT
}

void IdleController::onFastCallback() {
#if EFI_SHAFT_POSITION_INPUT
	float position = getIdlePosition(engine->triggerCentral.instantRpm.getInstantRpm());
	applyIACposition(position);
#endif // EFI_SHAFT_POSITION_INPUT
}

void IdleController::onConfigurationChange(engine_configuration_s const * previousConfiguration) {
#if ! EFI_UNIT_TEST
	shouldResetPid = !previousConfiguration || !m_pid.isSame(&previousConfiguration->idleRpmPid);
	mustResetPid = shouldResetPid;
#endif
}

void IdleController::init() {
	shouldResetPid = false;
	mightResetPid = false;
	m_pid.initPidClass(&engineConfiguration->idleRpmPid);
	m_timingPid.initPidClass(&engineConfiguration->idleTimingPid);

	m_timingHpf.configureHighpass(1000.0f / FAST_CALLBACK_PERIOD_MS, 1);
}

#endif /* EFI_IDLE_CONTROL */
