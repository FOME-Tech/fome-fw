/*
 * @file vw_b6.cpp
 *
 * @date Dec 26, 2019
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"

#include "vw_b6.h"
#include "table_helper.h"
#include "electronic_throttle.h"
#include "mre_meta.h"
#include "defaults.h"
#include "proteus_meta.h"

static inline void commonPassatB6() {
	setCrankOperationMode();
	engineConfiguration->trigger.type = trigger_type_e::TT_TOOTHED_WHEEL_60_2;
	engineConfiguration->vvtMode[0] = VVT_BOSCH_QUICK_START;
	engineConfiguration->map.sensor.type = MT_BOSCH_2_5;

    setTable(config->injectionPhase, -180.0f);

	engineConfiguration->etbIdleThrottleRange = 10;
	engineConfiguration->idlePidRpmDeadZone = 500;
	engineConfiguration->idleMode = IM_AUTO;

	engineConfiguration->cylindersCount = 4;
	engineConfiguration->firingOrder = FO_1_3_4_2;
	engineConfiguration->isPhaseSyncRequiredForIgnition = true;

	engineConfiguration->disableEtbWhenEngineStopped = true;


	for (int i = 4; i < MAX_CYLINDER_COUNT;i++) {
		engineConfiguration->injectionPins[i] = Gpio::Unassigned;
		engineConfiguration->ignitionPins[i] = Gpio::Unassigned;
	}

//	engineConfiguration->canNbcType = CAN_BUS_NBC_VAG;

	engineConfiguration->enableAemXSeries = true;


	// Injectors flow 1214 cc/min at 100 bar pressure
	engineConfiguration->injector.flow = 1214;
	// Use high pressure sensor
	engineConfiguration->injectorPressureType = IPT_High;
	// Automatic compensation of injector flow based on rail pressure
	engineConfiguration->injectorCompensationMode = ICM_SensedRailPressure;
	// Reference rail pressure is 10 000 kPa = 100 bar
	engineConfiguration->fuelReferencePressure = 10000;
	//setting "flat" 0.2 ms injector's lag time
	setArrayValues(engineConfiguration->injector.battLagCorr, 0.2);
	
	strcpy(engineConfiguration->engineMake, ENGINE_MAKE_VAG);
	strcpy(engineConfiguration->engineCode, "BPY");
	strcpy(engineConfiguration->vehicleName, "test");

	setPPSCalibration(0.36, 2.13, 0.73, 4.30);

	engineConfiguration->invertCamVVTSignal = true;

	/**
	 * PSS-140
	 */
	// todo: calibration
	engineConfiguration->highPressureFuel.v1 = 0.5; /* volts */;
	engineConfiguration->highPressureFuel.value1 = 0;
	engineConfiguration->highPressureFuel.v2 = 4.5; /* volts */;
	engineConfiguration->highPressureFuel.value2 = BAR2KPA(140);

	engineConfiguration->lowPressureFuel.v1 = 0.5; /* volts */;
	engineConfiguration->lowPressureFuel.value1 = PSI2KPA(0);
	engineConfiguration->lowPressureFuel.v2 = 4.5; /* volts */;
	// todo: what's the proper calibration of this Bosch sensor? is it really 200psi?
	engineConfiguration->lowPressureFuel.value2 = PSI2KPA(200);

	gppwm_channel *lowPressureFuelPumpControl = &engineConfiguration->gppwm[1];
	strcpy(engineConfiguration->gpPwmNote[1], "LPFP");
	lowPressureFuelPumpControl->pwmFrequency = 20;
	lowPressureFuelPumpControl->loadAxis = GPPWM_FuelLoad;
	lowPressureFuelPumpControl->dutyIfError = 50;
	setTable(lowPressureFuelPumpControl->table, (uint8_t)50);

	gppwm_channel *coolantControl = &engineConfiguration->gppwm[0];
	strcpy(engineConfiguration->gpPwmNote[0], "Rad Fan");
	coolantControl->loadAxis = GPPWM_Clt;

	coolantControl->pwmFrequency = 25;
	coolantControl->loadAxis = GPPWM_FuelLoad;
	// Volkswage wants 10% for fan to be OFF, between pull-up and low side control we need to invert that value
	// todo system lua for duty driven by CLT? (3, Gpio::E0, "0.15 90 coolant 120 min max 90 - 30 / 0.8 * +", 25);
	int value = 100 - 10;
	coolantControl->dutyIfError = value;
	setTable(coolantControl->table, (uint8_t)value);
	// for now I just want to stop radiator whine
	// todo: enable cooling!
	/*
    for (int load = 0; load < GPPWM_LOAD_COUNT; load++) {
		for (int r = 0; r < GPPWM_RPM_COUNT; r++) {
			engineConfiguration->gppwm[0].table[load][r] = value;
		}
	}
*/

	engineConfiguration->hpfpCamLobes = 3;
	engineConfiguration->hpfpPumpVolume = 0.290;
	engineConfiguration->hpfpMinAngle = 10;
	engineConfiguration->hpfpActivationAngle = 30;
	engineConfiguration->hpfpTargetDecay = 2000;
	engineConfiguration->hpfpPidP = 0.01;
	engineConfiguration->hpfpPidI = 0.0003;

	engineConfiguration->hpfpPeakPos = 10;

	setTable(config->veTable, 55);
	setBoschVAGETB();

	// random number just to take position away from zero
	engineConfiguration->vvtOffsets[0] = 180;

	// https://rusefi.com/forum/viewtopic.php?p=38235#p38235
	engineConfiguration->injector.flow = 1200;

	engineConfiguration->idle.solenoidPin = Gpio::Unassigned;
	engineConfiguration->fanPin = Gpio::Unassigned;

	engineConfiguration->injectionMode = IM_SEQUENTIAL;
	engineConfiguration->crankingInjectionMode = IM_SEQUENTIAL;
}


// MAF signal frequency after hardware divider x16, Hz
static const float hardCodedFreqBins[] = {139,
		152,
		180,
		217,
		280,
		300,
		365};

// MAF grams per second
static const float hardCodedGperSValues[] {
		3.58,
		4.5,
		6.7,
		11,
		22,
		25,
		40
};

/**
 * set engine_type 39
 */
void setProteusVwPassatB6() {
#if HW_PROTEUS
	static_assert(sizeof(hardCodedFreqBins) == sizeof(hardCodedGperSValues));
	{
		size_t mi = 0;
		for (; mi < efi::size(hardCodedFreqBins); mi++) {
			config->scriptCurve1Bins[mi] = hardCodedFreqBins[mi];
			config->scriptCurve1[mi] = hardCodedGperSValues[mi];
		}

		for (; mi < SCRIPT_CURVE_16; mi++) {
			config->scriptCurve1Bins[mi] = 3650 + mi;
			config->scriptCurve1[mi] = 4000;
		}
	}
	strcpy(engineConfiguration->scriptCurveName[0], "MAFcurve");


	commonPassatB6();
	engineConfiguration->triggerInputPins[0] = PROTEUS_VR_1;
	engineConfiguration->camInputs[0] = PROTEUS_DIGITAL_2;

	engineConfiguration->auxSpeedSensorInputPin[0] = PROTEUS_DIGITAL_5;

	engineConfiguration->lowPressureFuel.hwChannel = PROTEUS_IN_ANALOG_VOLT_5;
	engineConfiguration->highPressureFuel.hwChannel = PROTEUS_IN_ANALOG_VOLT_4;

	gppwm_channel *coolantControl = &engineConfiguration->gppwm[0];
	coolantControl->pin = PROTEUS_LS_5;

	engineConfiguration->mainRelayPin = PROTEUS_LS_6;

	gppwm_channel *lowPressureFuelPumpControl = &engineConfiguration->gppwm[1];
	lowPressureFuelPumpControl->pin = PROTEUS_LS_7;

	//engineConfiguration->boostControlPin = PROTEUS_LS_8;
	engineConfiguration->vvtPins[0] = PROTEUS_LS_9;
	engineConfiguration->hpfpValvePin = PROTEUS_LS_15;


	engineConfiguration->tps1_2AdcChannel = PROTEUS_IN_TPS1_2;
	setPPSInputs(PROTEUS_IN_ANALOG_VOLT_9, PROTEUS_IN_PPS2);

    #include "vw_b6.lua"

#endif
}

/**
 * set engine_type 62
 * VW_B6
 * has to be microRusEFI 0.5.2
 */
void setMreVwPassatB6() {
#if HW_MICRO_RUSEFI
	commonPassatB6();

	engineConfiguration->afr.hwChannel = MRE_IN_ANALOG_VOLT_10;

	engineConfiguration->tps1_2AdcChannel = MRE_IN_ANALOG_VOLT_9;



	// EFI_ADC_7: "31 - AN volt 3" - PA7
	// 36 - AN volt 8
	setPPSInputs(MRE_IN_ANALOG_VOLT_3, MRE_IN_ANALOG_VOLT_8);

	// "26 - AN volt 2"
	engineConfiguration->highPressureFuel.hwChannel = MRE_IN_ANALOG_VOLT_2;


	// "19 - AN volt 4"
	engineConfiguration->lowPressureFuel.hwChannel = EFI_ADC_12;

	engineConfiguration->isSdCardEnabled = false;

	engineConfiguration->mc33816spiDevice = SPI_DEVICE_3;
	// RED
	engineConfiguration->spi3mosiPin = Gpio::C12;
	// YELLOW
	engineConfiguration->spi3misoPin = Gpio::C11;
	// BROWN
	engineConfiguration->spi3sckPin = Gpio::C10;
	engineConfiguration->sdCardCsPin = Gpio::Unassigned;
	engineConfiguration->is_enabled_spi_3 = true;


	// J8 orange
	engineConfiguration->mc33816_cs = Gpio::B8;
	// J8 Grey
	engineConfiguration->mc33816_rstb = Gpio::A15;
	// J8 Dark BLUE
	engineConfiguration->mc33816_driven = Gpio::B9;
	// J9 violet
	engineConfiguration->mc33816_flag0 = Gpio::C13;

	// J10 Dark BLUE
	engineConfiguration->injectionPins[0] = Gpio::E6;
	// J11 green
	engineConfiguration->injectionPins[1] = Gpio::E5;
	// J18 grey
	engineConfiguration->injectionPins[2] = Gpio::B7;
	// J6 white
	engineConfiguration->injectionPins[3] = Gpio::E0;


	gppwm_channel *lowPressureFuelPumpControl = &engineConfiguration->gppwm[1];

	// "42 - Injector 4", somehow GP4 did not work? not enough current? not happy with diode?
	lowPressureFuelPumpControl->pin = MRE_INJ_4;


	gppwm_channel *coolantControl = &engineConfiguration->gppwm[0];

	coolantControl->pin = MRE_LS_2;
	// "7 - Lowside 1"
	//engineConfiguration->hpfpValvePin = MRE_LS_1;
	engineConfiguration->hpfpValvePin = Gpio::B10; // AUX J13


#endif /* HW_MICRO_RUSEFI */
}
