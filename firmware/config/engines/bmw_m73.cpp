/*
 * @file bmw_m73.cpp
 *
 * https://github.com/rusefi/rusefi/wiki/BMW_e38_750
 *
 * https://rusefi.com/wiki/index.php?title=Hardware:OEM_connectors#134_pin
 * https://github.com/rusefi/rusefi/wiki/HOWTO_electronic_throttle_body
 * Ignition module https://rusefi.com/forum/viewtopic.php?f=4&t=286
 *
 *
 * 1/2 plugs black
 * 2/2 plugs grey
 *
 *
 * ********* |    | OEM | rusEfi | function
 *
 * Plug #1 9 pin
 * ECU pin 4:  GND BRN/ORG
 * ECU pin 6:  GND BRN
 * ECU pin 7:  IN  RED         +12v hot at all times
 * ECU pin 8:  IN  RED/BLU     +12v from ECU relay
 *
 * Plug #2 24 pin
 * ECU pin 3:  CAN xxx/xxx     CAN low
 * ECU pin 4:  CAN xxx/xxx     CAN high
 * ECU pin 23: OUT BRN/BLK BLK ECU relay control, low-side
 *
 * Plug #3 52 pin
 * ECU pin 2:  OUT         WHT injector #4
 * ECU pin 6:  GND             ECU
 * ECU pin 15: OUT         BLK injector #2
 * ECU pin 20: IN          WHT hall effect camshaft sensor signal
 * ECU pin 21: GND BRN     BLK CLT sensor (only on first ECU)
 * ECU pin 22: IN  RED/BRN GRN CLT sensor (only on first ECU)
 * ECU pin 27: OUT         ORG injector #6
 * ECU pin 28: OUT         RED injector #5
 * ECU pin 32: IN          ORG VR positive crankshaft sensor - only 2x 5k per channel, R111 not installed, W1002 not installed
 * ECU pin 34: IN              IAT sensor (only on second ECU)
 * ECU pin 36: IN              Knock Sensor #2
 * ECU pin 37: GND             Knock Sensor GND
 * ECU pin 40: OUT BRN/BLK GRN injector #3
 * ECU pin 41: OUT BRN/WHT BLU injector #1
 * ECU pin 45: GND             crankshaft shield
 * ECU pin 46: IN  BLK     BLU VR negative crankshaft sensor
 * ECU pin 47: GND BRN     BLK IAT sensor Ground (only on second ECU)
 * ECU pin 49: IN              Knock Sensor #1
 * ECU pin 50: GND             Knock Sensor GND
 *
 * Plug #4 40 pin
 * ECU pin 6:  IN              start signal from ignition key
 * ECU pin 17: OUT BLK         engine speed output for gauge cluster
 * ECU pin 26: IN  GRN/BLK RED +12v hot in start & run
 * ECU pin 40: OUT YEL/BRN BRN starter enable
 *
 *
 * Plug #5 9 pin
 * ECU pin 3:  OUT BLK         coil signal, low-side
 * ECU pin 5:  GND BRN         ground
 * ECU pin 6:  OUT BLK         coil signal, low-side
 * ECU pin 9:  OUT BLK     RED coil signal, low-side
 *
 * Frankenso
 * set engine_type 40
 * Manhattan
 * set engine_type 24
 * Proteus
 * set engine_type 63
 *
 * https://raw.githubusercontent.com/wiki/rusefi/rusefi_documentation/oem_docs/VAG/Bosch_0280750009_pinout.jpg
 *
 * @date Nov 1, 2019
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"

#include "bmw_m73.h"
#include "custom_engine.h"

#if EFI_ELECTRONIC_THROTTLE_BODY
#include "electronic_throttle.h"
#endif // EFI_ELECTRONIC_THROTTLE_BODY

#if HW_PROTEUS
#include "proteus_meta.h"
#endif // HW_PROTEUS

void m73engine() {
	// 13641435991 injector
	engineConfiguration->injector.flow = 180; // cc/min, who knows if this number is real - no good source of info

	engineConfiguration->cylindersCount = 12;
	engineConfiguration->displacement = 5.4;
	strcpy(engineConfiguration->engineMake, ENGINE_MAKE_BMW);
	strcpy(engineConfiguration->engineCode, "M73");
	engineConfiguration->firingOrder = FO_1_7_5_11_3_9_6_12_2_8_4_10;
	engineConfiguration->fuelAlgorithm = LM_ALPHA_N;
	engineConfiguration->canNbcType = CAN_BUS_NBC_NONE;

	engineConfiguration->vvtMode[0] = VVT_SINGLE_TOOTH;

	engineConfiguration->globalTriggerAngleOffset = 90;
	setCrankOperationMode();
	// todo: that's not right, should be 60/2 without VW
	engineConfiguration->trigger.type = trigger_type_e::TT_60_2_VW;

	// this large engine seems to crank at around only 150 RPM? And happily idle at 400RPM?
	engineConfiguration->cranking.rpm = 350;

	engineConfiguration->ignitionMode = IM_TWO_COILS;

	// set cranking_fuel x
	engineConfiguration->cranking.baseFuel = 27;

	engineConfiguration->crankingTimingAngle = 15;
	setTable(config->veTable, 45);

	engineConfiguration->cylinderBore = 85.0;
}


// BMW_M73_F
void setBMW_M73_TwoCoilUnitTest() {
	// twoCoil configuration without unit tests ETB setup drama
	m73engine();
}

// BMW_M73_M
// set engine_type 24
void setEngineBMW_M73_Manhattan() {
	m73engine();

	/**
Nucleo boards - first step is to confirm that I can blink via each pin
going clockwise from top-right corner

Gpio::A10 USD ID
Gpio::A11 USD DM
Gpio::A12 USD DP

E_4: running

Good GPIO:
Gpio::C9 ETB#1
Gpio::C8 ETB#1
Gpio::B8 ETB#2
Gpio::B9 ETB#2
Gpio::C5
Gpio::A7
Gpio::A6
	 */


	engineConfiguration->fuelPumpPin = Gpio::Unassigned;
	engineConfiguration->idle.solenoidPin = Gpio::Unassigned;
	engineConfiguration->fanPin = Gpio::Unassigned;

	/**
	 * Yellow op-amp board
	 *
	 * AN5 tested pull-down 1M               PA3 TPS1 orange wire
	 * AN6 tested pull-down 1M               PA4 TPS2
	 * AN7 tested pull-down 1M               PA6 PPS
	 * AN8 tested no pull-down / no pull-up
	 */


	// For example TLE7209 - two control wires:
	// PWM on both wires - one to open, another to close
	// ETB motor NEG pin # - white wire - OUT 1
	// green input wire
	engineConfiguration->throttlePedalPositionAdcChannel = EFI_ADC_6;
	// set_analog_input_pin tps PA3
	engineConfiguration->tps1_1AdcChannel = EFI_ADC_3; // PA3
	// set_analog_input_pin tps2 PA4
	engineConfiguration->tps2_1AdcChannel = EFI_ADC_4; // PA4

	// PWM pin
	engineConfiguration->etbIo[0].controlPin = Gpio::Unassigned;
	// DIR pin
	engineConfiguration->etbIo[0].directionPin1 = Gpio::C8;
	engineConfiguration->etbIo[0].directionPin2 = Gpio::C9;
	engineConfiguration->etb_use_two_wires = true;

	// PWM pin
	engineConfiguration->etbIo[1].controlPin = Gpio::Unassigned;
	// DIR pin
	engineConfiguration->etbIo[1].directionPin1 = Gpio::B9;
	engineConfiguration->etbIo[1].directionPin2 = Gpio::B8;

	engineConfiguration->tps2Min = engineConfiguration->tpsMin;
	engineConfiguration->tps2Max = engineConfiguration->tpsMax;


	engineConfiguration->injectionPins[0] = Gpio::Unassigned;
	engineConfiguration->injectionPins[1] = Gpio::Unassigned;
	engineConfiguration->injectionPins[2] = Gpio::Unassigned;
	engineConfiguration->injectionPins[3] = Gpio::Unassigned;
	engineConfiguration->injectionPins[4] = Gpio::Unassigned;
	engineConfiguration->injectionPins[5] = Gpio::Unassigned;

	engineConfiguration->injectionPins[6] = Gpio::Unassigned;
	engineConfiguration->injectionPins[7] = Gpio::Unassigned;
	engineConfiguration->injectionPins[8] = Gpio::Unassigned;
	engineConfiguration->injectionPins[9] = Gpio::Unassigned;
	engineConfiguration->injectionPins[10] = Gpio::Unassigned;
	engineConfiguration->injectionPins[11] = Gpio::Unassigned;
}

/**
 * set engine_type 63
 *
 * https://github.com/mck1117/proteus/blob/master/readme_pinout.md
 *
 * black#3 : orange   : injector #1
 * black#4 : blue     : injector #3
 * black#5 : white    : injector #5
 * black#6 : green    : injector #6
 * black#7 : orange   : injector #7
 * black#8 : blue     : injector #9
 * black#9 : white    : injector #11
 * black#15: blue     : injector #2
 * black#16: white    : injector #4
 * black#19: green    : injector #8
 * black#20:          : injector #10
 * black#21:          : injector #12
 *
 *
 * small#5 :          : VR1 pos
 * small#8 : blue     : ETB1-
 * small#13: blue     : VR1 neg
 * small#15: orange   : ETB1+
 * small#18: red      : ignition power / ECU power source
 * small#19: black    : GND
 * small#21: blue     : ETB2-
 * small#22: orange   : ETB2+
 * small#23: red      : ETB/high-side power from main relay
 *
 *
 *
 * white#9 : orange   : +5v
 * white#17: green    : PPS
 * white#18: red      : TPS#2
 * white#23: black    : Sensor Ground
 * white#24: red      : TPS#1
 *
 */
#if HW_PROTEUS
void setEngineBMW_M73_Proteus() {
	m73engine();

	// 12 injectors defined in boards/proteus/board_configuration.cpp


	strcpy(engineConfiguration->vehicleName, "Using Proteus");

	// set_trigger_input_pin 0 PE7
	engineConfiguration->triggerInputPins[0] = PROTEUS_VR_1;

	// Gpio::E11: "Digital 2"
	engineConfiguration->camInputs[0] = PROTEUS_DIGITAL_2;

	// set vbatt_divider 8.16
	// engineConfiguration->vbattDividerCoeff = (49.0f / 10.0f) * 16.8f / 10.0f;
	// todo: figure out exact values from TLE8888 breakout board used by Manhattan
	// engineConfiguration->vbattDividerCoeff = 7.6; // is that Proteus 0.2 value?


	// no idea why https://github.com/rusefi/rusefi/wiki/HOWTO-M73-v12-on-Proteus uses non default CLT pin
	engineConfiguration->clt.adcChannel = PROTEUS_IN_ANALOG_TEMP_4;


	// Gpio::E0:  "Lowside 14"
	engineConfiguration->starterControlPin = Gpio::E0;
	// Gpio::E12: "Digital 3"
	engineConfiguration->startStopButtonPin = Gpio::E12;
	engineConfiguration->startStopButtonMode = PI_PULLUP;

	// tps and pps
	setProteusHitachiEtbDefaults();
}
#endif // HW_PROTEUS
