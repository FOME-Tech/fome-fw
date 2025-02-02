/**
 * @file ignition_state.cpp
 *
 * @date Mar 27, 2013
 * @author Andrey Belomutskiy, (c) 2012-2020
 *
 * This file is part of rusEfi - see http://rusefi.com
 *
 * rusEfi is free software; you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * rusEfi is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 * even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include "pch.h"

#include "idle_thread.h"
#include "launch_control.h"
#include "gppwm_channel.h"

#if EFI_ENGINE_CONTROL

// todo: reset this between cranking attempts?! #2735
float minCrankingRpm = 0;

/**
 * @return ignition timing angle advance before TDC
 */
static angle_t getRunningAdvance(float rpm, float engineLoad) {
	if (engineConfiguration->timingMode == TM_FIXED) {
		return engineConfiguration->fixedTiming;
	}

	if (std::isnan(engineLoad)) {
		warning(ObdCode::CUSTOM_NAN_ENGINE_LOAD, "NaN engine load");
		return NAN;
	}

	efiAssert(ObdCode::CUSTOM_ERR_ASSERT, !std::isnan(engineLoad), "invalid el", NAN);

	// compute base ignition angle from main table
	float advanceAngle = interpolate3d(
		config->ignitionTable,
		config->ignitionLoadBins, engineLoad,
		config->ignitionRpmBins, rpm
	);

#if EFI_ANTILAG_SYSTEM
	if (engine->antilagController.isAntilagCondition) {
		float throttleIntent = Sensor::getOrZero(SensorType::DriverThrottleIntent);
		engine->antilagController.timingALSCorrection = interpolate3d(
			config->ALSTimingRetardTable,
			config->alsIgnRetardLoadBins, throttleIntent,
			config->alsIgnRetardrpmBins, rpm
		);
		advanceAngle += engine->antilagController.timingALSCorrection;
	}
#endif /* EFI_ANTILAG_SYSTEM */

	// Add any adjustments if configured
	for (size_t i = 0; i < efi::size(config->ignBlends); i++) {
		auto result = calculateBlend(config->ignBlends[i], rpm, engineLoad);

		engine->outputChannels.ignBlendParameter[i] = result.BlendParameter;
		engine->outputChannels.ignBlendBias[i] = result.Bias;
		engine->outputChannels.ignBlendOutput[i] = result.Value;

		advanceAngle += result.Value;
	}

	// get advance from the separate table for Idle
#if EFI_IDLE_CONTROL
	if (engineConfiguration->useSeparateAdvanceForIdle &&
		engine->module<IdleController>()->isIdlingOrTaper()) {
		float idleAdvance = interpolate2d(rpm, config->idleAdvanceBins, config->idleAdvance);

		auto tps = Sensor::get(SensorType::DriverThrottleIntent);
		if (tps) {
			// interpolate between idle table and normal (running) table using TPS threshold
			// 0 TPS -> idle table
			// 1/2 threshold -> idle table
			// idle threshold -> normal table
			float idleThreshold = engineConfiguration->idlePidDeactivationTpsThreshold;
			advanceAngle = interpolateClamped(idleThreshold / 2, idleAdvance, idleThreshold, advanceAngle, tps.Value);
		}
	}
#endif

#if EFI_LAUNCH_CONTROL
	if (engine->launchController.isLaunchCondition && engineConfiguration->enableLaunchRetard) {
		if (engineConfiguration->launchSmoothRetard) {
			float launchAngle = engineConfiguration->launchTimingRetard;
			int launchRpm = engineConfiguration->launchRpm;
			int launchRpmWithTimingRange = launchRpm + engineConfiguration->launchTimingRpmRange;
			 // interpolate timing from rpm at launch triggered to full retard at launch launchRpm + launchTimingRpmRange
			return interpolateClamped(launchRpm, advanceAngle, launchRpmWithTimingRange, launchAngle, rpm);
		} else {
			return engineConfiguration->launchTimingRetard;
		}
	}
#endif /* EFI_LAUNCH_CONTROL */

	return advanceAngle;
}

void IgnitionState::updateAdvanceCorrections(float engineLoad) {
	if (auto iat = Sensor::get(SensorType::Iat)) {
		timingIatCorrection = interpolate3d(
			config->ignitionIatCorrTable,
			config->ignitionIatCorrLoadBins, engineLoad,
			config->ignitionIatCorrTempBins, iat.Value
		);
	} else {
		timingIatCorrection = 0;
	}

	if (auto clt = Sensor::get(SensorType::Clt)) {
		cltTimingCorrection = interpolate2d(
				clt.Value, config->cltTimingBins, config->cltTimingExtra
			);
	} else {
		cltTimingCorrection = 0;
	}

	#if EFI_SHAFT_POSITION_INPUT && EFI_IDLE_CONTROL
	float instantRpm = engine->triggerCentral.instantRpm.getInstantRpm();
	timingPidCorrection = engine->module<IdleController>()->getIdleTimingAdjustment(instantRpm);
	#endif // EFI_SHAFT_POSITION_INPUT && EFI_IDLE_CONTROL

	dfcoTimingRetard = engine->module<DfcoController>()->getTimingRetard();

	// TODO: multispark doesn't belong in corrections?
#if EFI_TUNER_STUDIO
	engine->outputChannels.multiSparkCounter = engine->engineState.multispark.count;
#endif /* EFI_TUNER_STUDIO */
}

angle_t IgnitionState::getAdvanceCorrections(bool isCranking) const {
	// Allow correction only if set to dynamic
	// AND we're either not cranking OR allowed to correct in cranking
	bool allowCorrections = engineConfiguration->timingMode == TM_DYNAMIC
		&& (!isCranking || engineConfiguration->useAdvanceCorrectionsForCranking);

	if (!allowCorrections) {
		return 0;
	}

	float result =
		  timingIatCorrection
		+ cltTimingCorrection
		+ timingPidCorrection
		- dfcoTimingRetard;

	return std::isnan(result) ? 0 : result;
}

/**
 * @return ignition timing angle advance before TDC for Cranking
 */
static angle_t getCrankingAdvance(float rpm, float engineLoad) {
	// get advance from the separate table for Cranking
	if (engineConfiguration->useSeparateAdvanceForCranking) {
		return interpolate2d(rpm, config->crankingAdvanceBins, config->crankingAdvance);
	}

	// Interpolate the cranking timing angle to the earlier running angle for faster engine start
	angle_t crankingToRunningTransitionAngle = getRunningAdvance(engineConfiguration->cranking.rpm, engineLoad);
	// interpolate not from zero, but starting from min. possible rpm detected
	if (rpm < minCrankingRpm || minCrankingRpm == 0) {
		minCrankingRpm = rpm;
	}

	return interpolateClamped(minCrankingRpm, engineConfiguration->crankingTimingAngle, engineConfiguration->cranking.rpm, crankingToRunningTransitionAngle, rpm);
}

angle_t IgnitionState::getAdvance(float rpm, float engineLoad, bool isCranking) {
	if (std::isnan(engineLoad)) {
		return 0; // any error should already be reported
	}

	angle_t angle;

	if (isCranking) {
		angle = getCrankingAdvance(rpm, engineLoad);
		assertAngleRange(angle, "crAngle", ObdCode::CUSTOM_ERR_ANGLE_CR);
		efiAssert(ObdCode::CUSTOM_ERR_ASSERT, !std::isnan(angle), "cr_AngleN", 0);
	} else {
		angle = getRunningAdvance(rpm, engineLoad);

		if (std::isnan(angle)) {
			warning(ObdCode::CUSTOM_ERR_6610, "NaN angle from table");
			return 0;
		}
	}

	angle += getAdvanceCorrections(isCranking);

	efiAssert(ObdCode::CUSTOM_ERR_ASSERT, !std::isnan(angle), "_AngleN5", 0);
	wrapAngle(angle, "getAdvance", ObdCode::CUSTOM_ERR_ADCANCE_CALC_ANGLE);
	return angle;
}

angle_t getCylinderIgnitionTrim(size_t cylinderNumber, float rpm, float ignitionLoad) {
	return interpolate3d(
		config->ignTrims[cylinderNumber].table,
		config->ignTrimLoadBins, ignitionLoad,
		config->ignTrimRpmBins, rpm
	);
}

size_t getMultiSparkCount(float rpm) {
	// Compute multispark (if enabled)
	if (engineConfiguration->multisparkEnable
		&& rpm <= engineConfiguration->multisparkMaxRpm
		&& engineConfiguration->multisparkMaxExtraSparkCount > 0) {
		// For zero RPM, disable multispark.  We don't yet know the engine speed, so multispark may not be safe.
		if (rpm == 0) {
			return 0;
		}

		floatus_t multiDelay = 1000.0f * engineConfiguration->multisparkSparkDuration;
		floatus_t multiDwell = 1000.0f * engineConfiguration->multisparkDwell;

		// dwell times are below 10 seconds here so we use 32 bit type for performance reasons
		engine->engineState.multispark.delay = efidur_t{(uint32_t)USF2NT(multiDelay)};
		engine->engineState.multispark.dwell = efidur_t{(uint32_t)USF2NT(multiDwell)};

		constexpr float usPerDegreeAt1Rpm = 60e6 / 360;
		floatus_t usPerDegree = usPerDegreeAt1Rpm / rpm;

		// How long is there for sparks? The user configured an angle, convert to time.
		floatus_t additionalSparksUs = usPerDegree * engineConfiguration->multisparkMaxSparkingAngle;
		// How long does one spark take?
		floatus_t oneSparkTime = multiDelay + multiDwell;

		// How many sparks can we fit in the alloted time?
		float sparksFitInTime = additionalSparksUs / oneSparkTime;

		// Take the floor (convert to uint8_t) - we want to undershoot, not overshoot
		uint32_t floored = sparksFitInTime;

		// Allow no more than the maximum number of extra sparks
		return minI(floored, engineConfiguration->multisparkMaxExtraSparkCount);
	} else {
		return 0;
	}
}

/**
 * @return Spark dwell time, in milliseconds. 0 if tables are not ready.
 */
floatms_t IgnitionState::getSparkDwell(float rpm, bool isCranking) {
	float dwellMs;
	if (isCranking) {
		dwellMs = engineConfiguration->ignitionDwellForCrankingMs;
	} else {
		efiAssert(ObdCode::CUSTOM_ERR_ASSERT, !std::isnan(rpm), "invalid rpm", NAN);

		baseDwell = interpolate2d(rpm, config->sparkDwellRpmBins, config->sparkDwellValues);
		dwellVoltageCorrection = interpolate2d(
				Sensor::getOrZero(SensorType::BatteryVoltage),
				config->dwellVoltageCorrVoltBins,
				config->dwellVoltageCorrValues
		);

		// for compat (table full of zeroes)
		if (dwellVoltageCorrection < 0.1f) {
			dwellVoltageCorrection = 1;
		}

		dwellMs = baseDwell * dwellVoltageCorrection;
	}

	if (std::isnan(dwellMs) || dwellMs <= 0) {
		// this could happen during engine configuration reset
		warning(ObdCode::CUSTOM_ERR_DWELL_DURATION, "invalid dwell: %.2f at rpm=%.0f", dwellMs, rpm);
		return 0;
	}

	return dwellMs;
}

void IgnitionState::updateDwell(float rpm, bool isCranking) {
	sparkDwell = getSparkDwell(rpm, isCranking);
	dwellAngle = std::isnan(rpm) ? NAN : getDwell() / getOneDegreeTimeMs(rpm);
}

floatms_t IgnitionState::getDwell() const {
	return sparkDwell;
}

#endif // EFI_ENGINE_CONTROL
