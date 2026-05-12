#include "g0_extension_firmware_impl.h"

#if HW_ATLAS && HAL_USE_SPI && G0_EXTENSION_FIRMWARE_IMAGE_AVAILABLE

namespace g0_extension_firmware {

uint32_t readLe32(const uint8_t* data) {
	return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
		   (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

void exchangeAppFrame(SPIDriver* spi, uint8_t command, uint8_t* rx) {
	memset(txBuffer(), 0, app::appFrameSize);
	txBuffer()[0] = command;
	memset(rxBuffer(), 0, app::appFrameSize);
	memset(rx, 0, app::appFrameSize);

	spiSelect(spi);
	spiExchange(spi, app::appFrameSize, txBuffer(), rxBuffer());
	spiUnselect(spi);

	memcpy(rx, rxBuffer(), app::appFrameSize);
}

bool isAppResponse(const uint8_t* rx, uint8_t expectedCommand, uint8_t expectedPayloadLength) {
	const bool knownStatus = rx[0] == app::statusReady || rx[0] == app::statusUpdateMode;
	return knownStatus && rx[1] == app::resultOk && rx[2] == expectedCommand && rx[3] == expectedPayloadLength;
}

bool readAppVersion(SPIDriver* spi, uint32_t& version) {
	uint8_t rx[app::appFrameSize];

	exchangeAppFrame(spi, app::cmdReadVersion, rx);
	chThdSleepMilliseconds(1);
	exchangeAppFrame(spi, app::cmdNop, rx);

	if (!isAppResponse(rx, app::cmdReadVersion, app::versionPayloadLength)) {
		efiPrintf(
				"G0 extension firmware: invalid version response %02X %02X %02X %02X",
				rx[0],
				rx[1],
				rx[2],
				rx[3]);
		return false;
	}

	version = readLe32(&rx[app::appHeaderSize]);
	return true;
}

void requestAppUpdate(SPIDriver* spi) {
	uint8_t rx[app::appFrameSize];

	exchangeAppFrame(spi, app::cmdEnterUpdate, rx);
	chThdSleepMilliseconds(1);
	exchangeAppFrame(spi, app::cmdNop, rx);
}

} // namespace g0_extension_firmware

#endif
