/**
 * @file boards/mikrus_df/board_configuration.cpp
 *
 * @brief Configuration defaults for the Mikrus DF board
 *        MCU: STM32H743VIT
 */

#include "pch.h"

// 8 injector outputs
static const brain_pin_e injPins[] = {
	Gpio::D7, // IN1
	Gpio::B4, // IN2
	Gpio::B5, // IN3
	Gpio::B6, // IN4
	Gpio::B7, // IN5
	Gpio::B8, // IN6
	Gpio::B9, // IN7
	Gpio::A8, // IN8
};

// 2 ignition outputs
static const brain_pin_e ignPins[] = {
	Gpio::D4, // IGN1
	Gpio::D3, // IGN2
};

static void setInjectorPins()
{
	for (size_t i = 0; i < efi::size(injPins); i++)
	{
		engineConfiguration->injectionPins[i] = injPins[i];
	}
}

static void setIgnitionPins()
{
	for (size_t i = 0; i < efi::size(ignPins); i++)
	{
		engineConfiguration->ignitionPins[i] = ignPins[i];
	}
}

// PE3 is the red error LED, defined in board.mk as LED_CRITICAL_ERROR_BRAIN_PIN
Gpio getCommsLedPin()
{
	return Gpio::E4; // GREEN
}

Gpio getRunningLedPin()
{
	return Gpio::E5; // GREEN
}

Gpio getWarningLedPin()
{
	return Gpio::E6; // ORANGE
}

// WiFi ATWINC1500 on SPI1
spi_device_e getWifiSpiDevice()
{
	return SPI_DEVICE_1;
}

Gpio getWifiCsPin()
{
	return Gpio::A4; // SPI_SSN
}

Gpio getWifiResetPin()
{
	return Gpio::B0; // RESET_N
}

Gpio getWifiIsrPin()
{
	return Gpio::B1; // IRQN
}

static void setupVbatt()
{
	// TODO: set analogInputDividerCoefficient to match the pull-up resistor
	// used on sensor inputs (e.g. 1.0 if there is no additional divider).
	engineConfiguration->analogInputDividerCoefficient = 1.0f;

	// TODO: measure the actual resistor divider on 12VSENSE and set this.
	// Example for 82k high side / 10k low side: (82+10)/10 = 9.2
	engineConfiguration->vbattDividerCoeff = 9.2f;

	// 12V sense on PA2 = EFI_ADC_2
	engineConfiguration->vbattAdcChannel = EFI_ADC_2;

	// REF31 precision 3.3V reference on VREF+
	engineConfiguration->adcVcc = 3.3f;
}

static void setupEtb()
{
	// TLE9201SG H-bridge driver
	// DIR  (PD10) - direction
	// DIS  (PD11) - disable (active high disables driver)
	// PWM  (PD12) - PWM / enable

	engineConfiguration->etbIo[0].controlPin = Gpio::D12;	 // PWM
	engineConfiguration->etbIo[0].directionPin1 = Gpio::D10; // DIR
	engineConfiguration->etbIo[0].disablePin = Gpio::D11;	 // DIS
	engineConfiguration->etbIo[0].directionPin2 = Gpio::Unassigned;

	// we only have pwm/dir, no dira/dirb
	engineConfiguration->etb_use_two_wires = false;
}

static void setupDefaultSensorInputs()
{
	// HALL1 (PC6) as primary trigger, HALL2 (PE11) as secondary/cam
	engineConfiguration->triggerInputPins[0] = Gpio::C6;  // HALL1
	engineConfiguration->triggerInputPins[1] = Gpio::E11; // HALL2

	// VR1 (PE7) is available as an alternative/additional trigger.
	// To use as primary VR crank input, reassign triggerInputPins[0] to Gpio::E7.

	// Default analog sensor assignments - adjust to match your wiring
	engineConfiguration->clt.adcChannel = EFI_ADC_10;		// AN3 PC0
	engineConfiguration->iat.adcChannel = EFI_ADC_11;		// AN4 PC1
	engineConfiguration->tps1_1AdcChannel = EFI_ADC_14;		// AN1 PC4
	engineConfiguration->map.sensor.hwChannel = EFI_ADC_15; // AN2 PC5

	// AN5 (PC2 = EFI_ADC_12) and AN6 (PC3 = EFI_ADC_13) are on ADC3.
	// AN7 (PA0 = EFI_ADC_0) and AN8 (PA1 = EFI_ADC_1) are free for wideband, etc.
}

void setBoardConfigOverrides()
{
	setupVbatt();

	engineConfiguration->canTxPin = Gpio::D1;
	engineConfiguration->canRxPin = Gpio::D0;

	// WiFi SPI1 - ATWINC1500
	engineConfiguration->is_enabled_spi_1 = true;
	engineConfiguration->spi1sckPin = Gpio::A5;	 // SPI_SCK
	engineConfiguration->spi1misoPin = Gpio::A6; // SPI_RXD
	engineConfiguration->spi1mosiPin = Gpio::A7; // SPI_TXD

	// SD card uses SDMMC1 (EFI_SDC_DEVICE=SDCD1) - disable SPI SD
	engineConfiguration->sdCardCsPin = Gpio::Unassigned;
	engineConfiguration->sdCardSpiDevice = SPI_NONE;
}

void setBoardDefaultConfiguration()
{
	setInjectorPins();
	setIgnitionPins();
	setupEtb();
	setupDefaultSensorInputs();

	engineConfiguration->isSdCardEnabled = true;

	engineConfiguration->canWriteEnabled = true;
	engineConfiguration->canBaudRate = B500KBPS;
}

void preHalInit()
{
	// Bootloader leaves SDMMC1 powered (POWER=3, 24 MHz bypass clock running).
	// Writing zeros to SDMMC registers in sdc_lld_start() is not enough —
	// the peripheral state machine needs a full RCC hardware reset.
	rccEnableSDMMC1(false);   // ensure clock gate is on before asserting reset
	SDMMC1->POWER = 0;        // stop clock output before reset
	rccResetSDMMC1();         // assert + deassert peripheral reset

	// Configure SDMMC1 pins (AF12).
	// Only 1-bit data bus is wired on this board revision (DAT0 only).
	// To upgrade to 4-bit, connect PC9=DAT1, PC10=DAT2, PC11=DAT3 and
	// change EFI_SDC_MODE to SDC_MODE_4BIT in board.mk.
	efiSetPadMode("SDMMC", Gpio::C8,  PAL_MODE_ALTERNATE(12) | PAL_STM32_OSPEED_HIGHEST | PAL_STM32_PUPDR_PULLUP); // DAT0
	efiSetPadMode("SDMMC", Gpio::C12, PAL_MODE_ALTERNATE(12) | PAL_STM32_OSPEED_HIGHEST | PAL_STM32_PUPDR_FLOATING); // CLK - no pullup on clock
	efiSetPadMode("SDMMC", Gpio::D2,  PAL_MODE_ALTERNATE(12) | PAL_STM32_OSPEED_HIGHEST | PAL_STM32_PUPDR_PULLUP); // CMD
	// DAT1-3 unused for data in 1-bit mode, but DAT3 must be pulled high so the card
	// selects SD mode (not SPI mode). DAT1/DAT2 pulled high per SD spec for 1-bit hosts.
	efiSetPadMode("SDMMC", Gpio::C9,  PAL_MODE_INPUT | PAL_STM32_PUPDR_PULLUP); // DAT1
	efiSetPadMode("SDMMC", Gpio::C10, PAL_MODE_INPUT | PAL_STM32_PUPDR_PULLUP); // DAT2
	efiSetPadMode("SDMMC", Gpio::C11, PAL_MODE_INPUT | PAL_STM32_PUPDR_PULLUP); // DAT3 - required for SD mode selection
}

#if EFI_BOOTLOADER
brain_pin_e getMisoPin(spi_device_e)
{
	return Gpio::A6;
}

brain_pin_e getMosiPin(spi_device_e)
{
	return Gpio::A7;
}

brain_pin_e getSckPin(spi_device_e)
{
	return Gpio::A5;
}
#endif // EFI_BOOTLOADER
