/**
 * @file	subaru.cpp
 *
 * SUBARU_2003_WRX
 * set engine_type 22
 *
 * @date Sep 14, 2014
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"

#include "subaru.h"
#include "custom_engine.h"
#include "defaults.h"

void setSubaru2003Wrx() {
	engineConfiguration->trigger.type = trigger_type_e::TT_TOOTHED_WHEEL;
	engineConfiguration->trigger.customTotalToothCount = 5;
	engineConfiguration->trigger.customSkippedToothCount = 1;

	engineConfiguration->useStepperIdle = true;

	// See http://rusefi.com/forum/viewtopic.php?f=4&t=1161
	engineConfiguration->idle.stepperDirectionPin = Gpio::D1; // top stepper drive pin, white wire recommended
	engineConfiguration->idle.stepperStepPin = Gpio::D6; // yellow wire recommended
	engineConfiguration->stepperEnablePin = Gpio::B1; // bottom stepper driver board pin, blue wire recommended

	engineConfiguration->triggerSimulatorPins[0] = Gpio::Unassigned; // we want to avoid PD1 conflict
	engineConfiguration->triggerSimulatorPins[1] = Gpio::Unassigned;
}

/**
 * MRE_SUBARU_EJ18
 * set engine_type 37
 */
void setSubaruEJ18_MRE() {
	engineConfiguration->trigger.type = trigger_type_e::TT_SUBARU_7_WITHOUT_6;


//	engineConfiguration->isDoubleSolenoidIdle = true;

	engineConfiguration->displacement = 1.8;
	strcpy(engineConfiguration->engineMake, ENGINE_MAKE_SUBARU);
	strcpy(engineConfiguration->engineCode, "EJ18");

	engineConfiguration->firingOrder = FO_1_3_2_4;
	engineConfiguration->injectionMode = IM_SEQUENTIAL;
	engineConfiguration->ignitionMode = IM_WASTED_SPARK;

#if (BOARD_TLE8888_COUNT > 0)
	// Gpio::TLE8888_PIN_23: "33 - GP Out 3"
	engineConfiguration->malfunctionIndicatorPin = Gpio::TLE8888_PIN_23;
#endif /* BOARD_TLE8888_COUNT */

	// this car has high-side main relay WOW so we have to hard wire it to ignition switch


}

/*
 * Subaru SVX (Alcyone SVX)
 */

void setSubaruEG33Defaults() {
	setCamOperationMode();

	engineConfiguration->trigger.type = trigger_type_e::TT_SUBARU_SVX;

	engineConfiguration->cylindersCount = 6;
	setLeftRightBanksNeedBetterName();
	engineConfiguration->firingOrder = FO_1_6_3_2_5_4;

	engineConfiguration->ignitionMode = IM_INDIVIDUAL_COILS;
	engineConfiguration->crankingInjectionMode = IM_SIMULTANEOUS;
	engineConfiguration->injectionMode = IM_SEQUENTIAL;

	/* TODO: */
	engineConfiguration->globalTriggerAngleOffset = 114;	// the end of 19th tooth?

	engineConfiguration->fuelAlgorithm = LM_REAL_MAF;

	engineConfiguration->displacement = 3.30;
	engineConfiguration->injector.flow = 250;

	engineConfiguration->cranking.baseFuel = 5;		// ???
	engineConfiguration->cranking.rpm = 400;

	engineConfiguration->rpmHardLimit = 6500;

	engineConfiguration->idleStepperReactionTime = 10;
	engineConfiguration->stepperDirectionPinMode = OM_INVERTED;
	engineConfiguration->useLinearCltSensor = true;

	engineConfiguration->canReadEnabled = true;
	engineConfiguration->canWriteEnabled = false;

	/* Fully closed - 0.9V, fully opened - 4.7 (?) */
	engineConfiguration->tpsMin = convertVoltageTo10bitADC(0.9);
	engineConfiguration->tpsMax = convertVoltageTo10bitADC(4.7);
	engineConfiguration->tpsErrorDetectionTooLow = -10; // -10% open
	engineConfiguration->tpsErrorDetectionTooHigh = 110; // 110% open

	engineConfiguration->mapMinBufferLength = 4;

	/* idle configuration */
	engineConfiguration->manIdlePosition = 30;
	engineConfiguration->idle.solenoidFrequency = 250;

	/* Check this */
	engineConfiguration->tachPulsePerRev = 2;
}
