/*
 * honda_k_dbc.cpp
 *
 * @date Oct 2, 2021
 * @author Andrey Belomutskiy, (c) 2012-2021
 */

#include "honda_k_dbc.h"

#if HW_PROTEUS & EFI_PROD_CODE
#include "proteus_meta.h"
#endif // HW_PROTEUS

void setHondaK() {
	engineConfiguration->cylindersCount = 4;
	engineConfiguration->displacement = 2.4;
	engineConfiguration->firingOrder = FO_1_3_4_2;
	engineConfiguration->engineSyncCam = SC_Exhaust_First;
	engineConfiguration->trigger.type = trigger_type_e::TT_HONDA_K_CRANK_12_1;
	engineConfiguration->globalTriggerAngleOffset = 675;

	engineConfiguration->vvtMode[0] = VVT_HONDA_K_INTAKE;
	engineConfiguration->vvtMode[1] = VVT_HONDA_K_EXHAUST;
	engineConfiguration->vvtOffsets[0] = -41;
	engineConfiguration->vvtOffsets[1] = 171;

	// set cranking_fuel 15
	engineConfiguration->cranking.baseFuel = 75;

	engineConfiguration->map.sensor.type = MT_DENSO183;
	engineConfiguration->injector.flow = 270;
	engineConfiguration->injectorCompensationMode = ICM_FixedRailPressure;
	engineConfiguration->fuelReferencePressure = 350; // TODO: what is real value?!

	engineConfiguration->ignitionMode = IM_INDIVIDUAL_COILS;
	engineConfiguration->crankingInjectionMode = IM_SIMULTANEOUS;
	engineConfiguration->injectionMode = IM_SEQUENTIAL;

	strcpy(engineConfiguration->engineMake, ENGINE_MAKE_HONDA);
	strcpy(engineConfiguration->engineCode, "K24");

	engineConfiguration->tpsMin = 100;
	engineConfiguration->tpsMax = 830;

	gppwm_channel *vtsControl = &engineConfiguration->gppwm[0];
	vtsControl->pwmFrequency = 0;
	strcpy(engineConfiguration->gpPwmNote[0], "VTS");
}


/**
 * K24A4 engine
 * PROTEUS_HONDA_K
 */
void setProteusHondaElement2003() {
    setHondaK();
//	engineConfiguration->trigger.type = trigger_type_e::TT_TOOTHED_WHEEL;
//	engineConfiguration->trigger.customTotalToothCount = 12;
//	engineConfiguration->trigger.customSkippedToothCount = 0;


//	engineConfiguration->trigger.type = trigger_type_e::TT_HONDA_K_CAM_4_1; // cam as primary, ignoring crank
//	engineConfiguration->globalTriggerAngleOffset = 570;



	strcpy(engineConfiguration->vehicleName, "test");

#if HW_PROTEUS & EFI_PROD_CODE
//	engineConfiguration->triggerInputPins[0] = PROTEUS_DIGITAL_2; // crank
//	engineConfiguration->camInputs[0] = PROTEUS_DIGITAL_4; // intake
//	engineConfiguration->camInputs[1 * CAMS_PER_BANK] = PROTEUS_DIGITAL_1; // exhaust

	engineConfiguration->triggerInputPins[0] = PROTEUS_DIGITAL_1; // exhaust
	engineConfiguration->camInputs[0] = PROTEUS_DIGITAL_4; // intake
// inverted
	// offset -41


	engineConfiguration->injectionPins[0] = PROTEUS_LS_8;
	engineConfiguration->injectionPins[1] = PROTEUS_LS_7;
	engineConfiguration->injectionPins[2] = PROTEUS_LS_6;
	engineConfiguration->injectionPins[3] = PROTEUS_LS_5;

	gppwm_channel *vtsControl = &engineConfiguration->gppwm[0];
	vtsControl->pin = PROTEUS_HS_1;
	engineConfiguration->vvtPins[0] = PROTEUS_HS_2;

	engineConfiguration->malfunctionIndicatorPin = PROTEUS_LS_10;
	engineConfiguration->idle.solenoidPin = PROTEUS_LS_15;
	engineConfiguration->fanPin = PROTEUS_LS_1;

	engineConfiguration->iat.adcChannel = PROTEUS_IN_ANALOG_TEMP_1;
	engineConfiguration->clt.adcChannel = PROTEUS_IN_ANALOG_TEMP_2;
	engineConfiguration->tps1_1AdcChannel = PROTEUS_IN_ANALOG_VOLT_3;
	engineConfiguration->map.sensor.hwChannel = PROTEUS_IN_ANALOG_VOLT_6;
	engineConfiguration->fanPin = Gpio::Unassigned;

	engineConfiguration->mainRelayPin = PROTEUS_LS_9;
	engineConfiguration->fuelPumpPin = PROTEUS_LS_11;

#endif // HW_PROTEUS
}

void setProteusHondaOBD2A() {

}
