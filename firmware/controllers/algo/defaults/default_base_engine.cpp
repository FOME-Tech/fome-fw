#include "pch.h"

#include "defaults.h"
#include "vr_pwm.h"

static void setDefaultAlternatorParameters() {
	engineConfiguration->targetVBatt = 14;

	engineConfiguration->alternatorControl.offset = 0;
	engineConfiguration->alternatorControl.pFactor = 30;
}

static void setDefaultTorqueModel() {
	// Closed-loop airmass trim. Output is a percent correction on the feed-forward throttle
	// flow, so gains are %-per-(gram of airmass error). Conservative defaults: the feed-forward
	// path already converges on its own, the trim just removes steady-state model error.
	engineConfiguration->torqueModel.airmassTrimKp = 10;
	engineConfiguration->torqueModel.airmassTrimKi = 20;
	engineConfiguration->torqueModel.airmassTrimAuthority = 25;

	engineConfiguration->torqueModel.torqueLossLoadAxis = GPPWM_Clt;
	setLinearCurve(config->torqueLossRpmBins, 800, 7000, 1);
	setLinearCurve(config->torqueLossLoadBins, -20, 100, 1);
}

static void setDefaultTractionControl() {
	auto& tc = engineConfiguration->tractionControl;

	// Slip control PID. Error is slip speed (kph), output is permitted axle torque (Nm), so the gains
	// are axle-Nm per kph and need no speed scheduling (the plant gain to slip speed, r/I_eff, is
	// speed-independent). These are rough starting points for a ~0.3 m tyre in a mid gear; the real
	// scaling is the driveline inertia I_eff, which a tuner adjusts via the gains. minValue/maxValue
	// bound the commandable axle torque; keep maxValue well above any realistic axle demand so the
	// per-tick dynamic iTermMax (= live axle demand) is what actually rails the output.
	tc.slipPid.pFactor = 80;  // axle-Nm per kph
	tc.slipPid.iFactor = 200; // axle-Nm per kph/s
	tc.slipPid.dFactor = 5;	  // axle-Nm·s per kph
	tc.slipPid.minValue = 0;
	tc.slipPid.maxValue = 30000;

	tc.minimumSpeed = 5;
	tc.engineTorqueFloor = 0;
	tc.engageRate = 0;	   // unlimited bite by default
	tc.releaseRate = 2000; // axle Nm/s handback
	tc.slipTargetMax = 25;

	setLinearCurve(config->slipTargetSpeedBins, 0, 200, 1);
	setLinearCurve(config->slipTargetTrimBins, 0, 3, 1);
	for (size_t s = 0; s < efi::size(config->slipTargetSpeedBins); s++) {
		for (size_t t = 0; t < efi::size(config->slipTargetTrimBins); t++) {
			config->slipTargetTable[s][t] = 10;
		}
	}
}

/* Cylinder to bank mapping */
void setLeftRightBanksNeedBetterName() {
	for (size_t i = 0; i < engineConfiguration->cylindersCount; i++) {
		engineConfiguration->cylinderBankSelect[i] = i % 2;
	}
}

void setDefaultBaseEngine() {
	// Base Engine Settings
	engineConfiguration->cylindersCount = 4;
	engineConfiguration->displacement = 2;
	engineConfiguration->firingOrder = FO_1_3_4_2;

	engineConfiguration->instantRpmRange = 90;
	engineConfiguration->cylContributionWindow = 90;
	engineConfiguration->cylContributionPhase = 180;

	engineConfiguration->compressionRatio = 9;

	engineConfiguration->turbochargerFilter = 0.01f;

	engineConfiguration->fuelAlgorithm = LM_SPEED_DENSITY;

	// Limits and Fallbacks
	engineConfiguration->rpmHardLimit = 7000;
	engineConfiguration->rpmHardLimitHyst = 50;
	engineConfiguration->cutFuelOnHardLimit = true;
	engineConfiguration->cutSparkOnHardLimit = false;
	engineConfiguration->etbRevLimitRange = 250;

	// CLT RPM limit table - just the X axis
	copyArray(config->cltRevLimitRpmBins, {-20, 0, 40, 80});

	engineConfiguration->ALSMinRPM = 400;
	engineConfiguration->ALSMaxRPM = 3200;
	engineConfiguration->ALSMaxDuration = 3.5;
	engineConfiguration->ALSMaxCLT = 105;
	//	engineConfiguration->alsMinPps = 10;
	engineConfiguration->alsMinTimeBetween = 5;
	engineConfiguration->alsEtbPosition = 30;
	engineConfiguration->ALSMaxTPS = 5;

	// Trigger
	engineConfiguration->trigger.type = trigger_type_e::TT_TOOTHED_WHEEL_60_2;

	engineConfiguration->globalTriggerAngleOffset = 0;

	// Default this to on - if you want to diagnose, turn it off.
	engineConfiguration->silentTriggerError = true;

	// Advanced Trigger

	// Battery and alternator
	engineConfiguration->vbattDividerCoeff = ((float)(15 + 65)) / 15;

#if EFI_ALTERNATOR_CONTROL
	setDefaultAlternatorParameters();
#endif /* EFI_ALTERNATOR_CONTROL */

	setDefaultTorqueModel();
	setDefaultTractionControl();

	// Fuel pump
	engineConfiguration->startUpFuelPumpDuration = 4;

	engineConfiguration->benchTestOnTime = 4;
	engineConfiguration->benchTestOffTime = 500;
	engineConfiguration->benchTestCount = 3;

	engineConfiguration->ignTestOnTime = 3;
	engineConfiguration->ignTestOffTime = 500;
	engineConfiguration->ignTestCount = 3;

	// Fans
	engineConfiguration->fanOnTemperature = 95;
	engineConfiguration->fanOffTemperature = 91;
	engineConfiguration->fan2OnTemperature = 95;
	engineConfiguration->fan2OffTemperature = 91;

	// Tachometer
	// 50% duty cycle is the default for tach signal
	engineConfiguration->tachPulseDurationAsDutyCycle = true;
	engineConfiguration->tachPulseDuractionMs = 0.5;
	engineConfiguration->tachPulsePerRev = 1;

	engineConfiguration->etbMinimumPosition = 1;
	engineConfiguration->etbMaximumPosition = 99;

	// Check engine light
#if EFI_PROD_CODE
	engineConfiguration->warningPeriod = 10;
#else
	engineConfiguration->warningPeriod = 0;
#endif /* EFI_PROD_CODE */

	setDefaultVrThresholds();

	// Oil pressure protection
	engineConfiguration->minimumOilPressureTimeout = 0.5f;
	engineConfiguration->oilPressureProtectionStartDelay = 2.0f;
	setLinearCurve(config->minimumOilPressureBins, 0, 7000);
}

void setPPSInputs(adc_channel_e pps1, adc_channel_e pps2) {
	engineConfiguration->throttlePedalPositionAdcChannel = pps1;
	engineConfiguration->throttlePedalPositionSecondAdcChannel = pps2;
}

void setTPS1Inputs(adc_channel_e tps1, adc_channel_e tps2) {
	engineConfiguration->tps1_1AdcChannel = tps1;
	engineConfiguration->tps1_2AdcChannel = tps2;
}

void setTPS1Calibration(uint16_t tpsMin, uint16_t tpsMax, uint16_t tps1SecondaryMin, uint16_t tps1SecondaryMax) {
	engineConfiguration->tpsMin = tpsMin;
	engineConfiguration->tpsMax = tpsMax;

	engineConfiguration->tps1SecondaryMin = tps1SecondaryMin;
	engineConfiguration->tps1SecondaryMax = tps1SecondaryMax;
	engineConfiguration->tpsSecondaryMaximum = 100; // fully redundant
}

void setPPSCalibration(float primaryUp, float primaryDown, float secondaryUp, float secondaryDown) {
	engineConfiguration->throttlePedalUpVoltage = primaryUp;
	engineConfiguration->throttlePedalWOTVoltage = primaryDown;
	engineConfiguration->throttlePedalSecondaryUpVoltage = secondaryUp;
	engineConfiguration->throttlePedalSecondaryWOTVoltage = secondaryDown;
	engineConfiguration->ppsSecondaryMaximum = 100; // fully redundant
}

void setEtbPID(float p, float i, float d) {
	engineConfiguration->etb.pFactor = p;
	engineConfiguration->etb.iFactor = i;
	engineConfiguration->etb.dFactor = d;
}
