#include "pch.h"

#include "adc_subscription.h"

#ifndef EFI_BOOTLOADER
#include "functional_sensor.h"
#include "linear_func.h"
#endif

static const brain_pin_e injPins[] = {
	Gpio::G5,
	Gpio::G6,
	Gpio::G7,
	Gpio::G8,
	Gpio::C6,
	Gpio::C7,
	Gpio::A15,
	Gpio::D3,
	Gpio::B4,
	Gpio::B5,
	Gpio::B6,
	Gpio::B7,
};

static const brain_pin_e ignPins[] = {
	Gpio::B3,
	Gpio::G15,
	Gpio::G14,
	Gpio::G13,
	Gpio::G12,
	Gpio::G11,
	Gpio::G10,
	Gpio::G9,
	Gpio::D7,
	Gpio::D6,
	Gpio::D5,
	Gpio::D4,
};

static void setInjectorPins() {
	copyArray(engineConfiguration->injectionPins, injPins);
}

static void setIgnitionPins() {
	copyArray(engineConfiguration->ignitionPins, ignPins);
}

Gpio getCommsLedPin() {
	return Gpio::F2;
}

Gpio getRunningLedPin() {
	return Gpio::D14;
}

Gpio getWarningLedPin() {
	return Gpio::F1;
}

spi_device_e getWifiSpiDevice() {
	return SPI_DEVICE_4;
}

Gpio getWifiCsPin() {
	return Gpio::E4;
}

Gpio getWifiResetPin() {
	return Gpio::C13;
}

Gpio getWifiIsrPin() {
	return Gpio::E3;
}

static void setupVbatt() {
	// 5.6k high side/10k low side = 1.56 ratio divider
	engineConfiguration->analogInputDividerCoefficient = 1.56f;

	// 82k high side/10k low side = 9.2
	engineConfiguration->vbattDividerCoeff = (92.0f / 10.0f);

	// Battery sense on PC5
	engineConfiguration->vbattAdcChannel = EFI_ADC_15;

	engineConfiguration->adcVcc = 3.3f;
}

static void setupEtb() {
	// TLE9201 driver
	// This chip has three control pins:
	// DIR - sets direction of the motor
	// PWM - pwm control (enable high, coast low)
	// DIS - disables motor (enable low)

	// Throttle #1
	// PWM pin
	engineConfiguration->etbIo[0].controlPin = Gpio::D12;
	// DIR pin
	engineConfiguration->etbIo[0].directionPin1 = Gpio::D10;
	// Disable pin
	engineConfiguration->etbIo[0].disablePin = Gpio::D11;
	// Unused
	engineConfiguration->etbIo[0].directionPin2 = Gpio::Unassigned;

	// Throttle #2
	// PWM pin
	engineConfiguration->etbIo[1].controlPin = Gpio::D13;
	// DIR pin
	engineConfiguration->etbIo[1].directionPin1 = Gpio::D9;
	// Disable pin
	engineConfiguration->etbIo[1].disablePin = Gpio::D8;
	// Unused
	engineConfiguration->etbIo[1].directionPin2 = Gpio::Unassigned;

	// we only have pwm/dir, no dira/dirb
	engineConfiguration->etb_use_two_wires = false;
}

void setBoardConfigOverrides() {
	setupVbatt();

	engineConfiguration->clt.config.bias_resistor = 2700;
	engineConfiguration->iat.config.bias_resistor = 2700;

	engineConfiguration->canTxPin = Gpio::D1;
	engineConfiguration->canRxPin = Gpio::D0;
	engineConfiguration->can2RxPin = Gpio::B12;
	engineConfiguration->can2TxPin = Gpio::B13;

	engineConfiguration->lps25BaroSensorScl = Gpio::B10;
	engineConfiguration->lps25BaroSensorSda = Gpio::B11;

	// WiFi SPI
	engineConfiguration->is_enabled_spi_4 = true;
	engineConfiguration->spi4sckPin = Gpio::E2;
	engineConfiguration->spi4misoPin = Gpio::E5;
	engineConfiguration->spi4mosiPin = Gpio::E6;
}

void setBoardDefaultConfiguration() {
	setupEtb();
	setInjectorPins();
	setIgnitionPins();

	engineConfiguration->isSdCardEnabled = true;

	engineConfiguration->enableSoftwareKnock = true;
}

void preHalInit() {
	efiSetPadMode("SDMMC",  Gpio::C8, PAL_MODE_ALTERNATE(12) | PAL_STM32_OSPEED_HIGHEST | PAL_STM32_PUPDR_PULLUP);
	efiSetPadMode("SDMMC",  Gpio::C9, PAL_MODE_ALTERNATE(12) | PAL_STM32_OSPEED_HIGHEST | PAL_STM32_PUPDR_PULLUP);
	efiSetPadMode("SDMMC", Gpio::C10, PAL_MODE_ALTERNATE(12) | PAL_STM32_OSPEED_HIGHEST | PAL_STM32_PUPDR_PULLUP);
	efiSetPadMode("SDMMC", Gpio::C11, PAL_MODE_ALTERNATE(12) | PAL_STM32_OSPEED_HIGHEST | PAL_STM32_PUPDR_PULLUP);
	efiSetPadMode("SDMMC", Gpio::C12, PAL_MODE_ALTERNATE(12) | PAL_STM32_OSPEED_HIGHEST | PAL_STM32_PUPDR_PULLUP);
	efiSetPadMode("SDMMC",  Gpio::D2, PAL_MODE_ALTERNATE(12) | PAL_STM32_OSPEED_HIGHEST | PAL_STM32_PUPDR_PULLUP);
}

#ifndef EFI_BOOTLOADER
void initBoardSensors() {
	{
		static LinearFunc mrSenseFunc;
		static FunctionalSensor mrSenseSensor(SensorType::MainRelayVoltage, MS2NT(100));

		// 82k high side/10k low side = 9.2
		const float mrSenseRatio = (92.0f / 10.0f);

		mrSenseFunc.configure(0, 0, 1, mrSenseRatio, 0, 50);
		mrSenseSensor.setFunction(mrSenseFunc);
		AdcSubscription::SubscribeSensor(mrSenseSensor, EFI_ADC_16, /*bandwidth*/ 20, /*ratio*/ 1);
		mrSenseSensor.Register();
	}

	{
		static LinearFunc sensor5vFunc;
		static FunctionalSensor sensor5vSensor(SensorType::Sensor5vVoltage, MS2NT(100));

		const float sensor5vRatio = 2;;

		sensor5vFunc.configure(0, 0, 1, sensor5vRatio, 0, 50);
		sensor5vSensor.setFunction(sensor5vFunc);
		AdcSubscription::SubscribeSensor(sensor5vSensor, EFI_ADC_17, /*bandwidth*/ 20, /*ratio*/ 1);
		sensor5vSensor.Register();
	}
}
#endif
