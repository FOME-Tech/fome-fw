#pragma once
namespace g0 {
// Startup only, before registering the runtime SPI device and any remote pins.
bool updateFirmware(SPIDriver& spi, const SPIConfig& spiSettings, brain_pin_e reset, brain_pin_e boot);
} // namespace g0
