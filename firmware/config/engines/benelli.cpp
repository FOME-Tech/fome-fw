/*
 * @file benelli.cpp
 *
 * set engine_type 33
 *
 * @date Feb 13, 2024
 * @author Michael Holzer, (c) 2024
*/

#include "pch.h"
#include "table_helper.h"
#include "electronic_throttle.h"
#include "mre_meta.h"
#include "defaults.h"

#include "benelli.h"
#include "custom_engine.h"

void setMreConfiguration() {
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

void set_Mre_Benelli_Tre() {
	setMreConfiguration();

	setWholeTimingTable_d(20);
	// set cranking_timing_angle 10
	engineConfiguration->crankingTimingAngle = 10; // ToDo:verify

	// set global_trigger_offset_angle 93
	engineConfiguration->globalTriggerAngleOffset = 93; // ToDo:verify


	setCrankOperationMode();
	engineConfiguration->trigger.type = trigger_type_e::TT_Benelli_Tre;

	engineConfiguration->mafAdcChannel = EFI_ADC_1; // ToDo:verify


	//Base engine setting
	engineConfiguration->cylindersCount = 3;
	engineConfiguration->firingOrder = FO_1_2_3; // ToDo:verify
  engineConfiguration->injector.flow = 320; // 30lb/h // ToDo:verify
	// set algorithm 3
	setAlgorithm(LM_SPEED_DENSITY);
	engineConfiguration->map.sensor.type = MT_GM_3_BAR; // ToDo:verify

	engineConfiguration->ignitionMode = IM_INDIVIDUAL_COILS;

	engineConfiguration->ignitionPins[0] = Gpio::E14; // ToDo:verify
	engineConfiguration->ignitionPins[1] = Gpio::Unassigned;
	engineConfiguration->ignitionPins[2] = Gpio::Unassigned;
	engineConfiguration->ignitionPins[3] = Gpio::Unassigned;

	engineConfiguration->wastegatePositionSensor = EFI_ADC_4; // PA4 // ToDo:verify

	float mapRange = 110; // ToDo:verify

	setEgoSensor(ES_PLX); // ToDo:verify
	setFuelTablesLoadBin(20, mapRange); // ToDo:verify
	setLinearCurve(config->ignitionLoadBins, 20, mapRange);

	engineConfiguration->isSdCardEnabled = false;
	engineConfiguration->tpsMin = 0;
	engineConfiguration->tpsMax = 100;
}

void set_Benelli_3_cyl_900ccm() {
	engineConfiguration->displacement = 0.9;
}

void set_Benelli_3_cyl_1130ccm() {
  engineConfiguration->displacement = 1.13;
}
