//#include "hellen_meta.h"
//#include "defaults.h"

#include "pch.h"

static void setInjectorPins() {
	engineConfiguration->injectionPins[0] = Gpio::G7;
	engineConfiguration->injectionPins[1] = Gpio::G8;
	engineConfiguration->injectionPins[2] = Gpio::D11;
	engineConfiguration->injectionPins[3] = Gpio::D10;
	engineConfiguration->injectionPins[4] = Gpio::D9;
	engineConfiguration->injectionPins[5] = Gpio::F12;
}

static void setIgnitionPins() {
	engineConfiguration->ignitionPins[0] = Gpio::B9;
	engineConfiguration->ignitionPins[1] = Gpio::Unassigned;
	engineConfiguration->ignitionPins[2] = Gpio::Unassigned;
	engineConfiguration->ignitionPins[3] = Gpio::Unassigned;
	engineConfiguration->ignitionPins[4] = Gpio::Unassigned;
	engineConfiguration->ignitionPins[5] = Gpio::Unassigned;
}

//static void setEtbConfig() {
	// TLE9201 driver
	// This chip has three control pins:
	// DIR - sets direction of the motor
	// PWM - pwm control (enable high, coast low)
	// DIS - disables motor (enable low)

	// Throttle #1
	// PWM pin
	//engineConfiguration->etbIo[0].controlPin = Gpio::B8;
	// DIR pin
	//engineConfiguration->etbIo[0].directionPin1 = Gpio::B9;
	// Disable pin
	//engineConfiguration->etbIo[0].disablePin = Gpio::B7;

	// Throttle #2
	// PWM pin
	//engineConfiguration->etbIo[1].controlPin = Gpio::Unassigned;
	// DIR pin
	//engineConfiguration->etbIo[1].directionPin1 = Gpio::Unassigned;
	// Disable pin
	//engineConfiguration->etbIo[1].disablePin = Gpio::Unassigned;

	// we only have pwm/dir, no dira/dirb
	//engineConfiguration->etb_use_two_wires = false;
//}

static void setupVbatt() {
	// 5.6k high side/10k low side = 1.56 ratio divider
	engineConfiguration->analogInputDividerCoefficient = 1.56f;
	
	// 6.34k high side/ 1k low side
	engineConfiguration->vbattDividerCoeff = (6.34 + 1) / 1; 

	// Battery sense on PA7
	engineConfiguration->vbattAdcChannel = EFI_ADC_0;

	engineConfiguration->adcVcc = 3.3f;
}

// PE3 is error LED, configured in board.mk
Gpio getCommsLedPin() {
	return Gpio::G1;
}

Gpio getRunningLedPin() {
	return Gpio::E7;
}

Gpio getWarningLedPin() {
	return Gpio::E8;
}


void AccelerometerPreInitCS2Pin() {
#if EFI_ONBOARD_MEMS
    if (!accelerometerChipSelect.isInitialized()) {
	    accelerometerChipSelect.initPin("mm-CS2", Gpio::H_SPI1_CS2);
	    accelerometerChipSelect.setValue(1);
	}
#endif // EFI_ONBOARD_MEMS
}

static void setupDefaultSensorInputs() {
	engineConfiguration->tps1_1AdcChannel = EFI_ADC_4;
 	engineConfiguration->clt.adcChannel = EFI_ADC_12;
	engineConfiguration->iat.adcChannel = EFI_ADC_13;
	engineConfiguration->mafAdcChannel = EFI_ADC_14;
	engineConfiguration->vehicleSpeedSensorInputPin = Gpio::F11;
	engineConfiguration->triggerInputPins[0] = Gpio::B1;
	engineConfiguration->camInputs[0] = Gpio::A6;
	engineConfiguration->clutchDownPin = Gpio::F5;
	engineConfiguration->clutchDownPinMode = PI_PULLDOWN;
	engineConfiguration->vbattAdcChannel = EFI_ADC_0;
	engineConfiguration->vbattDividerCoeff = (33 + 6.8) / 6.8; // 5.835
	engineConfiguration->adcVcc = 3.29f;
	engineConfiguration->analogInputDividerCoefficient = 2.0f;
		
}


  //init5vpDiag(); // piggy back on popular 'setHellenVbatt' method



void setBoardConfigOverrides() {
	setupVbatt();
	//setEtbConfig();

	engineConfiguration->clt.config.bias_resistor = 4700;
	engineConfiguration->iat.config.bias_resistor = 4700;
	engineConfiguration->auxTempSensor1.config.bias_resistor = 4700;
	engineConfiguration->auxTempSensor2.config.bias_resistor = 4700;

	//CAN 1 bus overwrites
	engineConfiguration->canRxPin = Gpio::D0;
	engineConfiguration->canTxPin = Gpio::D1;

	//CAN 2 bus overwrites
	engineConfiguration->can2RxPin = Gpio::B5;
	engineConfiguration->can2TxPin = Gpio::B6;
}

void setBoardDefaultConfiguration() {
	setInjectorPins();
	setIgnitionPins();
	setupDefaultSensorInputs();

	//engineConfiguration->displayLogicLevelsInEngineSniffer = true;
	engineConfiguration->globalTriggerAngleOffset = 9;
	engineConfiguration->enableSoftwareKnock = true;
	engineConfiguration->cylindersCount = 6;
	engineConfiguration->firingOrder = FO_1_4_2_5_3_6;
	engineConfiguration->ignitionMode = IM_ONE_COIL;
	engineConfiguration->fuelAlgorithm = LM_REAL_MAF;
	engineConfiguration->mainRelayPin = Gpio::E10;
 	engineConfiguration->fanPin = Gpio::G6;
	engineConfiguration->fuelPumpPin = Gpio::G4;
	engineConfiguration->idle.solenoidPin = Gpio::G3;
	engineConfiguration->tachOutputPin = Gpio::F13;
	engineConfiguration->enableVerboseCanTx = true;

	engineConfiguration->accelerometerSpiDevice = SPI_DEVICE_1;
	engineConfiguration->accelerometerCsPin = Gpio::H_SPI1_CS2;

	engineConfiguration->lps25BaroSensorScl = Gpio::B10;
	engineConfiguration->lps25BaroSensorSda = Gpio::B11;

  	engineConfiguration->sdCardCsPin = Gpio::H_SPI1_CS1;
  	engineConfiguration->sdCardSpiDevice = SPI_DEVICE_1;
	engineConfiguration->spi1mosiPin = Gpio::H_SPI1_MOSI;
	engineConfiguration->spi1misoPin = Gpio::H_SPI1_MISO;
	engineConfiguration->spi1sckPin = Gpio::H_SPI1_SCK;
	engineConfiguration->is_enabled_spi_1 = true;
 	engineConfiguration->isSdCardEnabled = true;

	setCrankOperationMode();
	
	engineConfiguration->injectorCompensationMode = ICM_FixedRailPressure;
	setCommonNTCSensor(&engineConfiguration->clt, 4700);
	setCommonNTCSensor(&engineConfiguration->iat, 4700);
    	//setTPS1Calibration(75, 900);
	engineConfiguration->enableAemXSeries = true;
}
