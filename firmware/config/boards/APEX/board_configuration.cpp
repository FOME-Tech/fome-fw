/**
 * @file boards/apex/board_configuration.cpp
 *
 * @brief Configuration defaults for the core48 board
 *
 * @author Turbo Marian,  2022
 */

#include "pch.h"

static void setInjectorPins() {
	engineConfiguration->injectionPinMode = OM_DEFAULT;

	engineConfiguration->injectionPins[0] = Gpio::G6;
	engineConfiguration->injectionPins[1] = Gpio::G5;
	engineConfiguration->injectionPins[2] = Gpio::G4;
	engineConfiguration->injectionPins[3] = Gpio::G3;
	engineConfiguration->injectionPins[4] = Gpio::G2;
	engineConfiguration->injectionPins[5] = Gpio::D15;
	engineConfiguration->injectionPins[6] = Gpio::D14;
	engineConfiguration->injectionPins[7] = Gpio::D13;
}

static void setIgnitionPins() {
	engineConfiguration->ignitionPinMode = OM_DEFAULT;

	engineConfiguration->ignitionPins[0] = Gpio::E15;
	engineConfiguration->ignitionPins[1] = Gpio::E14;
	engineConfiguration->ignitionPins[2] = Gpio::E13;
	engineConfiguration->ignitionPins[3] = Gpio::E12;
	engineConfiguration->ignitionPins[4] = Gpio::E11;
	engineConfiguration->ignitionPins[5] = Gpio::E10;
	engineConfiguration->ignitionPins[6] = Gpio::E9;
	engineConfiguration->ignitionPins[7] = Gpio::E8;
}


// PE3 is error LED, configured in board.mk
Gpio getCommsLedPin() {
	return Gpio::B7;
}

Gpio getRunningLedPin() {
	return Gpio::B8;
}

Gpio getWarningLedPin() {
	return Gpio::B9;
}

static void setEtbConfig() {
	// TLE9201 driver
	// This chip has three control pins:
	// DIR - sets direction of the motor
	// PWM - pwm control (enable high, coast low)
	// DIS - disables motor (enable low)

	// Throttle #1
	// PWM pin
	engineConfiguration->etbIo[0].controlPin = Gpio::E7;
	// DIR pin
	engineConfiguration->etbIo[0].directionPin1 = Gpio::G0;
	// Disable pin
	engineConfiguration->etbIo[0].disablePin = Gpio::G1;
	// Unused
	engineConfiguration->etbIo[0].directionPin2 = Gpio::Unassigned;

	// Throttle #2
	// PWM pin
	engineConfiguration->etbIo[1].controlPin = Gpio::F15;
	// DIR pin
	engineConfiguration->etbIo[1].directionPin1 = Gpio::F13;
	// Disable pin
	engineConfiguration->etbIo[1].disablePin = Gpio::F14;
	// Unused
	engineConfiguration->etbIo[1].directionPin2 = Gpio::Unassigned;

	// we only have pwm/dir, no dira/dirb
	engineConfiguration->etb_use_two_wires = false;
}

static void 
setupVbatt() {
	// 5.6k high side/10k low side = 1.56 ratio divider
	engineConfiguration->analogInputDividerCoefficient = 1.6f;
	
	// 6.34k high side/ 1k low side
	engineConfiguration->vbattDividerCoeff = (6.34 + 1) / 1; 

	// Battery sense on PA7
	engineConfiguration->vbattAdcChannel = EFI_ADC_0;

	engineConfiguration->adcVcc = 3.3f;
}

static void setStepperConfig() {
	engineConfiguration->idle.stepperDirectionPin = Gpio::Unassigned;
	engineConfiguration->idle.stepperStepPin = Gpio::Unassigned;
	engineConfiguration->stepperEnablePin = Gpio::Unassigned;
}

static void setupSdCard() {
	
	//SD CARD overwrites
	engineConfiguration->sdCardSpiDevice = SPI_DEVICE_2;		

	engineConfiguration->is_enabled_spi_2 = true;
	engineConfiguration->spi2sckPin = Gpio::B13;
	engineConfiguration->spi2misoPin = Gpio::B14;
	engineConfiguration->spi2mosiPin = Gpio::B15;
}

static void setupEGT() {
	
	//EGT overwrites

	engineConfiguration->spi1sckPin = Gpio::B3;
	engineConfiguration->spi1misoPin = Gpio::B4;
	engineConfiguration->spi1mosiPin = Gpio::Unassigned;
	engineConfiguration->is_enabled_spi_1 = true;

	engineConfiguration->max31855spiDevice = SPI_DEVICE_1;
	engineConfiguration->max31855_cs[0] = Gpio::D2;
	engineConfiguration->max31855_cs[1] = Gpio::D3;
}


void setBoardConfigOverrides() {
	setupVbatt();
	setupSdCard();
	setEtbConfig();
	setStepperConfig();
	setupEGT();

	engineConfiguration->clt.config.bias_resistor = 2490;
	engineConfiguration->iat.config.bias_resistor = 2490;

	//SERIAL 
	engineConfiguration->binarySerialTxPin = Gpio::D5;
	engineConfiguration->binarySerialRxPin = Gpio::D6;
	engineConfiguration->tunerStudioSerialSpeed = 230400;
	//engineConfiguration->uartConsoleSerialSpeed = SERIAL_SPEED;



	//CAN 1 bus overwrites
	engineConfiguration->canRxPin = Gpio::D0;
	engineConfiguration->canTxPin = Gpio::D1;

	//CAN 2 bus overwrites
	engineConfiguration->can2RxPin = Gpio::B5;
	engineConfiguration->can2TxPin = Gpio::B6;

	//onboard lps22 barometer
	engineConfiguration->lps25BaroSensorScl = Gpio::A8;
	engineConfiguration->lps25BaroSensorSda = Gpio::C9;
}

static void setupDefaultSensorInputs() {

	engineConfiguration->afr.hwChannel = EFI_ADC_11; //PC1
	engineConfiguration->afr.hwChannel2 = EFI_ADC_10; //PC0
	setEgoSensor(ES_14Point7_Free);
	
	engineConfiguration->map.sensor.hwChannel = EFI_ADC_12; //PC2
	engineConfiguration->map.sensor.type = MT_MPXH6400;
	
	engineConfiguration->baroSensor.hwChannel = EFI_ADC_NONE;

}


void setBoardDefaultConfiguration(void) {
	setInjectorPins();
	setIgnitionPins();
	setupDefaultSensorInputs();


	engineConfiguration->canWriteEnabled = true;
	engineConfiguration->canReadEnabled = true;
	engineConfiguration->canSleepPeriodMs = 50;

	engineConfiguration->canBaudRate = B500KBPS;
	engineConfiguration->can2BaudRate = B500KBPS;

	engineConfiguration->sdCardCsPin = Gpio::G7;
		
	strncpy(config->luaScript, R"(

	function onTick()

	end

    )", efi::size(config->luaScript));
}