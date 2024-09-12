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

static void setupVbatt() {
	engineConfiguration->analogInputDividerCoefficient = 2.0f;
	engineConfiguration->vbattDividerCoeff = (33 + 6.8) / 6.8; 
	engineConfiguration->vbattAdcChannel = EFI_ADC_0;
	engineConfiguration->adcVcc = 3.29f;
}

static void enableSpi1() {
	engineConfiguration->spi1mosiPin = Gpio::B5;
	engineConfiguration->spi1misoPin = Gpio::B4;
	engineConfiguration->spi1sckPin = Gpio::B3;
	engineConfiguration->is_enabled_spi_1 = true;
}

void AccelerometerPreInitCS2Pin() {
#if EFI_ONBOARD_MEMS
    if (!accelerometerChipSelect.isInitialized()) {
	    accelerometerChipSelect.initPin("mm-CS2", Gpio::B7);
	    accelerometerChipSelect.setValue(1);
	}
#endif // EFI_ONBOARD_MEMS
}


static void setAccelerometerSpi() {
	/* accelerometer SPI is shared with SD card SPI on mm144 */
	engineConfiguration->accelerometerSpiDevice = SPI_DEVICE_1;
	engineConfiguration->accelerometerCsPin = Gpio::B7;
}

static void setSdCardSpi1Hardware() {
  engineConfiguration->sdCardCsPin = Gpio::B6;
  engineConfiguration->sdCardSpiDevice = SPI_DEVICE_1;
}

static void setSdCardSpi1() {
  engineConfiguration->isSdCardEnabled = true;
}

static void setDefaultAtPullUps(){
	engineConfiguration->clt.config.bias_resistor = 4700;
	engineConfiguration->iat.config.bias_resistor = 4700;
	engineConfiguration->auxTempSensor1.config.bias_resistor = 4700;
	engineConfiguration->auxTempSensor2.config.bias_resistor = 4700;
}

static void setBaro() {
	engineConfiguration->lps25BaroSensorScl = Gpio::B10;
	engineConfiguration->lps25BaroSensorSda = Gpio::B11;
}

static void setCAN() {
	engineConfiguration->canRxPin = Gpio::D0;
	engineConfiguration->canTxPin = Gpio::D1;
	engineConfiguration->can2RxPin = Gpio::B12;
	engineConfiguration->can2TxPin = Gpio::B13;
	engineConfiguration->enableVerboseCanTx = true;
}

static void setEtbConfig() {
	// TLE9201 driver
	// This chip has three control pins:
	// DIR - sets direction of the motor
	// PWM - pwm control (enable high, coast low)
	// DIS - disables motor (enable low)

	// Throttle #1
	// PWM pin
	engineConfiguration->etbIo[0].controlPin = Gpio::D3;
	// DIR pin
	engineConfiguration->etbIo[0].directionPin1 = Gpio::G11;
	// Disable pin
	engineConfiguration->etbIo[0].disablePin = Gpio::D2;

	// Throttle #2
	// PWM pin
	engineConfiguration->etbIo[1].controlPin = Gpio::G13;
	// DIR pin
	engineConfiguration->etbIo[1].directionPin1 = Gpio::A9;
	// Disable pin
	engineConfiguration->etbIo[1].disablePin = Gpio::G12;

	// we only have pwm/dir, no dira/dirb
	engineConfiguration->etb_use_two_wires = false;
}

// PG0 is error LED, configured in board.mk
Gpio getCommsLedPin() {
	return Gpio::G1;
}

Gpio getRunningLedPin() {
	return Gpio::E7;
}

Gpio getWarningLedPin() {
	return Gpio::E8;
}


static void setupDefaultSensorInputs() {
	engineConfiguration->tps1_1AdcChannel = EFI_ADC_4;
 	engineConfiguration->clt.adcChannel = EFI_ADC_12;
	engineConfiguration->iat.adcChannel = EFI_ADC_13;
	engineConfiguration->mafAdcChannel = EFI_ADC_14;
	engineConfiguration->triggerInputPins[0] = Gpio::B1;
	engineConfiguration->camInputs[0] = Gpio::A6;
	engineConfiguration->clutchDownPin = Gpio::F5;
	engineConfiguration->clutchDownPinMode = PI_PULLUP;
  //engineConfiguration->vehicleSpeedSensorInputPin = Gpio::F11;
}


void setBoardConfigOverrides() {
	setupVbatt();
	
	enableSpi1();
	setAccelerometerSpi();
	AccelerometerPreInitCS2Pin();
	setSdCardSpi1Hardware();
	setSdCardSpi1();

	setDefaultAtPullUps();
	setBaro();
	setEtbConfig();
	setCAN();
}

void setBoardDefaultConfiguration() {
	setInjectorPins();
	setIgnitionPins();
	setupDefaultSensorInputs();
	setCrankOperationMode();
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
	engineConfiguration->injectorCompensationMode = ICM_FixedRailPressure;
	engineConfiguration->enableAemXSeries = true;

}
