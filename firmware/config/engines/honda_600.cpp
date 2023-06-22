/*
 * @file honda_600.cpp
 *
 * set engine_type 43
 *
 * @date Jul 9, 2016
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"

#include "engine_template.h"
#include "honda_600.h"
#include "custom_engine.h"

#if EFI_PROD_CODE
#include "can_hw.h"
#endif

static void setDefaultCustomMaps() {
	setTimingLoadBin(0,100);
	setTimingRpmBin(0,7000);
}

void setHonda600() {

	engineConfiguration->trigger.type = trigger_type_e::TT_HONDA_CBR_600;
	engineConfiguration->fuelAlgorithm = LM_ALPHA_N;

	// upside down wiring
	engineConfiguration->triggerInputPins[0] = Gpio::A5;
	engineConfiguration->triggerInputPins[1] = Gpio::C6;


	// set global_trigger_offset_angle 180
	// set global_trigger_offset_angle 540
	engineConfiguration->globalTriggerAngleOffset = 540;

    engineConfiguration->cylindersCount = 4;
    engineConfiguration->crankingInjectionMode = IM_SIMULTANEOUS;
	engineConfiguration->injectionMode = IM_SEQUENTIAL;
  engineConfiguration->firingOrder = FO_1_3_4_2;
  engineConfiguration->cranking.rpm = 800;
//	engineConfiguration->ignitionMode = IM_WASTED_SPARK; //IM_INDIVIDUAL_COILS;

	engineConfiguration->ignitionMode = IM_INDIVIDUAL_COILS;


  //setIndividualCoilsIgnition();

	commonFrankensoAnalogInputs();
	setTable(config->injectionPhase, 320.0f);

	/**
	 * Frankenso analog #1 PC2 ADC12 CLT
	 * Frankenso analog #2 PC1 ADC11 IAT
	 * Frankenso analog #3 PA0 ADC0 MAP
	 * Frankenso analog #4 PC3 ADC13 WBO / O2
	 * Frankenso analog #5 PA2 ADC2 TPS
	 * Frankenso analog #6 PA1 ADC1
	 * Frankenso analog #7 PA4 ADC4
	 * Frankenso analog #8 PA3 ADC3
	 * Frankenso analog #9 PA7 ADC7
	 * Frankenso analog #10 PA6 ADC6
	 * Frankenso analog #11 PC5 ADC15
	 * Frankenso analog #12 PC4 ADC14 VBatt
	 */
	engineConfiguration->tps1_1AdcChannel = EFI_ADC_2;

	engineConfiguration->map.sensor.hwChannel = EFI_ADC_0;

	engineConfiguration->clt.adcChannel = EFI_ADC_12;
	engineConfiguration->iat.adcChannel = EFI_ADC_11;
	engineConfiguration->afr.hwChannel = EFI_ADC_13;

	setCommonNTCSensor(&engineConfiguration->clt, 2700);
	setCommonNTCSensor(&engineConfiguration->iat, 2700);


	/**
	 * http://rusefi.com/wiki/index.php?title=Manual:Hardware_Frankenso_board
	 */
	// Frankenso low out #1: PE6
	// Frankenso low out #2: PE5
	// Frankenso low out #3: PD7 Main Relay
	// Frankenso low out #4: PC13 Idle valve solenoid
	// Frankenso low out #5: PE3
	// Frankenso low out #6: PE4 fuel pump relay
	// Frankenso low out #7: PE1 (do not use with discovery!)
	// Frankenso low out #8: PE2 injector #2
	// Frankenso low out #9: PB9 injector #1
	// Frankenso low out #10: PE0 (do not use with discovery!)
	// Frankenso low out #11: PB8 injector #3
	// Frankenso low out #12: PB7 injector #4

	engineConfiguration->fuelPumpPin = Gpio::E4;
	engineConfiguration->mainRelayPin = Gpio::D7;
	engineConfiguration->idle.solenoidPin = Gpio::C13;

	engineConfiguration->fanPin = Gpio::E5;

	engineConfiguration->injectionPins[0] = Gpio::B9; // #1
	engineConfiguration->injectionPins[1] = Gpio::D5; // #2
	engineConfiguration->injectionPins[2] = Gpio::B7; // #3
	engineConfiguration->injectionPins[3] = Gpio::B8; // #4

	setDefaultCustomMaps();
	setAlgorithm(LM_ALPHA_N);

	engineConfiguration->injectionPins[4] = Gpio::Unassigned;
	engineConfiguration->injectionPins[5] = Gpio::Unassigned;
	engineConfiguration->injectionPins[6] = Gpio::Unassigned;
	engineConfiguration->injectionPins[7] = Gpio::Unassigned;
	engineConfiguration->injectionPins[8] = Gpio::Unassigned;
	engineConfiguration->injectionPins[9] = Gpio::Unassigned;
	engineConfiguration->injectionPins[10] = Gpio::Unassigned;
	engineConfiguration->injectionPins[11] = Gpio::Unassigned;

	engineConfiguration->ignitionPins[0] = Gpio::E14;
	engineConfiguration->ignitionPins[1] = Gpio::C7;
	engineConfiguration->ignitionPins[2] = Gpio::E10;
	engineConfiguration->ignitionPins[3] = Gpio::C9; // #4

	// todo: 8.2 or 10k?
	engineConfiguration->vbattDividerCoeff = ((float) (10 + 33)) / 10 * 2;
}

