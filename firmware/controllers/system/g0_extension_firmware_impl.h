#pragma once

#include "pch.h"
#include "../../ext/g0_firmware/for_fome/g0_spi_protocol.h"

#if HW_ATLAS && HAL_USE_SPI

#if __has_include("../../ext/g0_firmware/for_fome/g0_firmware_image.h")
#include "../../ext/g0_firmware/for_fome/g0_firmware_image.h"
#define G0_EXTENSION_FIRMWARE_IMAGE_AVAILABLE TRUE
#else
#define G0_EXTENSION_FIRMWARE_IMAGE_AVAILABLE FALSE
#endif

namespace g0_extension_firmware {

namespace app = ::g0_spi_protocol;

static constexpr spi_device_e SpiDevice = SPI_DEVICE_5;
static constexpr brain_pin_e ResetPin = Gpio::B14;
static constexpr brain_pin_e BootPin = Gpio::B15;
static constexpr brain_pin_e SpiCsPin = Gpio::F6;
static constexpr uint32_t FlashBase = 0x08000000;

static constexpr uint8_t BootloaderAck = 0x79;
static constexpr uint8_t BootloaderNack = 0x1F;
static constexpr uint8_t BootloaderSync = 0x5A;
static constexpr uint8_t BootloaderExtendedErase = 0x44;
static constexpr uint8_t BootloaderWriteMemory = 0x31;
static constexpr uint8_t BootloaderGo = 0x21;
static constexpr int BootloaderInterByteDelayUs = 20;
static constexpr int FlashMaxAttempts = 2;

static constexpr size_t SpiDmaBufferSize = 258;

SPIConfig& spiConfig();
uint8_t* txBuffer();
uint8_t* rxBuffer();

void setControlPin(brain_pin_e pin, bool value);
void initControlPins();
void releaseControlPins();
void resetExtension(bool bootloaderMode);

uint8_t spiByte(SPIDriver* spi, uint8_t tx);
uint8_t bootloaderSpiByte(SPIDriver* spi, uint8_t tx);
bool handleBootloaderAck(SPIDriver* spi, uint8_t response);
bool waitBootloaderAck(SPIDriver* spi, int attempts = 200);
bool sendBootloaderBytesAndWaitAck(SPIDriver* spi, const uint8_t* data, size_t size);
bool sendBootloaderCommand(SPIDriver* spi, uint8_t command);
bool sendBootloaderAddress(SPIDriver* spi, uint32_t address);
bool bootloaderMassErase(SPIDriver* spi);
bool bootloaderWriteBlock(SPIDriver* spi, uint32_t address, const uint8_t* data, size_t size);
bool bootloaderGo(SPIDriver* spi, uint32_t address);
bool flashFirmwareImage(SPIDriver* spi);
bool flashFirmwareImageWithRetry(SPIDriver* spi);

void exchangeAppFrame(SPIDriver* spi, uint8_t command, app::AppFrame& rx);
bool isAppResponse(const app::AppFrame& rx, uint8_t expectedCommand, uint8_t expectedPayloadLength);
bool readAppVersion(SPIDriver* spi, uint32_t& version);
void requestAppUpdate(SPIDriver* spi);

} // namespace g0_extension_firmware

#endif
