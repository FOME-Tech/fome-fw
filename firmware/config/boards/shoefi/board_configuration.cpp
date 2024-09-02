#include "pch.h"
#include "hellen_meta.h"
#include "defaults.h"
#include "hellen_leds_144.cpp"

static void setInjectorPins() {
	engineConfiguration->injectionPins[0] = H144_LS_1;
	engineConfiguration->injectionPins[1] = H144_LS_2;
	engineConfiguration->injectionPins[2] = H144_LS_3;
	engineConfiguration->injectionPins[3] = H144_LS_4;
	engineConfiguration->injectionPins[4] = H144_LS_5;
	engineConfiguration->injectionPins[5] = H144_LS_6;
}

static void setIgnitionPins() {
	engineConfiguration->ignitionPins[0] = H144_IGN_7;
	engineConfiguration->ignitionPins[1] = Gpio::Unassigned;
	engineConfiguration->ignitionPins[2] = Gpio::Unassigned;
	engineConfiguration->ignitionPins[3] = Gpio::Unassigned;
	engineConfiguration->ignitionPins[4] = Gpio::Unassigned;
	engineConfiguration->ignitionPins[5] = Gpio::Unassigned;
}

static void setupVbatt() {
	// 5.6k high side/1k low side = 1.56 ratio divider
	engineConfiguration->analogInputDividerCoefficient = 1.56f;
	// set vbatt_divider
	// 6.34k high side/ 1k low side
	engineConfiguration->vbattDividerCoeff = (6.34 + 1) / 1;
	// Battery sense on PA0
	engineConfiguration->vbattAdcChannel = EFI_ADC_0;
	engineConfiguration->adcVcc = 3.3f;
}

static void setupDefaultSensorInputs() {
	// trigger inputs, hall
	engineConfiguration->triggerInputPins[0] = H144_IN_CRANK;
	engineConfiguration->camInputs[0] = H144_IN_CAM;
	engineConfiguration->tps1_1AdcChannel = H144_IN_TPS;
	engineConfiguration->mafAdcChannel = EFI_ADC_10;
	engineConfiguration->afr.hwChannel = EFI_ADC_1;
	engineConfiguration->clt.adcChannel = H144_IN_CLT;
	engineConfiguration->iat.adcChannel = H144_IN_IAT;
}

void setBoardConfigOverrides() {
	setupVbatt();
	setHellenSdCardSpi2();
	setDefaultHellenAtPullUps();
	setHellenCan();
}

void setBoardDefaultConfiguration() {

	engineConfiguration->fuelPumpPin = Gpio::G4;	// OUT_IO8
	engineConfiguration->idle.solenoidPin = Gpio::G3;	// OUT_IO7
	engineConfiguration->fanPin = Gpio::G6;	// OUT_IO13
	engineConfiguration->mainRelayPin = Gpio::E10;
    	engineConfiguration->tachOutputPin = Gpio::F13;
	engineConfiguration->clutchDownPin = Gpio::F5; // Clutch switch input
	engineConfiguration->clutchDownPinMode = PI_PULLDOWN;
	engineConfiguration->launchActivationMode = CLUTCH_INPUT_LAUNCH;
	engineConfiguration->malfunctionIndicatorPin = Gpio::Unassigned;
	engineConfiguration->cylindersCount = 6;
	engineConfiguration->firingOrder = FO_1_4_2_5_3_6;
	engineConfiguration->ignitionMode = IM_SINGLE_COIL;
	//setupDefaultSensorInputs();	
}
