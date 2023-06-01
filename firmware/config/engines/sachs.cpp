/**
 * @file	sachs.cpp
 *
 * set engine_type 29
 * http://rusefi.com/forum/viewtopic.php?f=3&t=396
 *
 * @date Jan 26, 2015
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"

#include "sachs.h"

void setSachs() {

	engineConfiguration->displacement = 0.1; // 100cc
	engineConfiguration->cylindersCount = 1;

	setTwoStrokeOperationMode();
	engineConfiguration->firingOrder = FO_1;
	engineConfiguration->engineChartSize = 400;

	setEgoSensor(ES_Innovate_MTX_L);

	/**
	 * 50/2 trigger
	 */
	engineConfiguration->trigger.type = trigger_type_e::TT_TOOTHED_WHEEL;
	engineConfiguration->trigger.customTotalToothCount = 50;
	engineConfiguration->trigger.customSkippedToothCount = 2;

	// Frankenstein analog input #1: PA1 adc1 MAP
	// Frankenstein analog input #2: PA3 adc3 TPS
	// Frankenstein analog input #3: PC3 adc13 IAT
	// Frankenstein analog input #4: PC1 adc11 CLT
	// Frankenstein analog input #5: PA0 adc0 O2
	// Frankenstein analog input #6: PC2 adc12
	// Frankenstein analog input #7: PA4 adc4
	// Frankenstein analog input #8: PA2 adc2
	// Frankenstein analog input #9: PA6 adc6
	// Frankenstein analog input #10: PA7 adc7
	// Frankenstein analog input #11: PC4 adc14
	// Frankenstein analog input #12: PC5 adc15

	engineConfiguration->tps1_1AdcChannel = EFI_ADC_3;
	engineConfiguration->vbattAdcChannel = EFI_ADC_NONE;

	/**
	 * TPS 0% 0.9v
	 * TPS 100% 2.34v
	 */
	engineConfiguration->tpsMin = convertVoltageTo10bitADC(1.250);
	engineConfiguration->tpsMax = convertVoltageTo10bitADC(4.538);


	// Frankenstein: low side - out #1: PC14
	// Frankenstein: low side - out #2: PC15
	// Frankenstein: low side - out #3: PE6
	// Frankenstein: low side - out #4: PC13
	// Frankenstein: low side - out #5: PE4
	// Frankenstein: low side - out #6: PE5
	// Frankenstein: low side - out #7: PE2
	// Frankenstein: low side - out #8: PE3
	// Frankenstein: low side - out #9: PE0
	// Frankenstein: low side - out #10: PE1
	// Frankenstein: low side - out #11: PB8
	// Frankenstein: low side - out #12: PB9

	engineConfiguration->triggerInputPins[0] = Gpio::A5;
	engineConfiguration->triggerInputPins[1] = Gpio::Unassigned;

	engineConfiguration->injectionPins[0] = Gpio::C15;

	engineConfiguration->fuelPumpPin = Gpio::E6;

	// todo: extract a method? figure out something smarter
	setTimingRpmBin(800, 15000);
	setLinearCurve(config->veRpmBins, 7000, 15000, 1);
	setLinearCurve(config->lambdaRpmBins, 500, 7000, 1);
}
