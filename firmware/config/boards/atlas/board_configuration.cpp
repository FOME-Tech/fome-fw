#include "pch.h"

Gpio getCommsLedPin() {
	return Gpio::B7;
}

Gpio getRunningLedPin() {
	return Gpio::B0;
}

Gpio getWarningLedPin() {
	// this board has no warning led
	return Gpio::Unassigned;
}

spi_device_e getWifiSpiDevice() {
	return SPI_DEVICE_3;
}

Gpio getWifiCsPin() {
	return Gpio::D2;
}

Gpio getWifiResetPin() {
	return Gpio::G3;
}

Gpio getWifiIsrPin() {
	return Gpio::G2;
}

void setBoardConfigOverrides() {
	engineConfiguration->is_enabled_spi_3 = true;
	engineConfiguration->spi3sckPin = Gpio::C10;
	engineConfiguration->spi3misoPin = Gpio::C11;
	engineConfiguration->spi3mosiPin = Gpio::C12;
}
