#include "g0_extension_firmware_impl.h"

#if HW_ATLAS && HAL_USE_SPI && G0_EXTENSION_FIRMWARE_IMAGE_AVAILABLE

namespace g0_extension_firmware {

uint32_t readLe32(const uint8_t* data) {
	return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
		   (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

void exchangeAppFrame(SPIDriver* spi, uint8_t command, uint8_t* rx) {
	memset(txBuffer(), 0, AppFrameSize);
	txBuffer()[0] = command;
	memset(rxBuffer(), 0, AppFrameSize);
	memset(rx, 0, AppFrameSize);

	spiSelect(spi);
	spiExchange(spi, AppFrameSize, txBuffer(), rxBuffer());
	spiUnselect(spi);

	memcpy(rx, rxBuffer(), AppFrameSize);
}

bool isAppResponse(const uint8_t* rx, uint8_t expectedCommand, uint8_t expectedPayloadLength) {
	const bool knownStatus = rx[0] == AppStatusReady || rx[0] == AppStatusUpdateMode;
	return knownStatus && rx[1] == AppResultOk && rx[2] == expectedCommand && rx[3] == expectedPayloadLength;
}

bool readAppVersion(SPIDriver* spi, uint32_t& version) {
	uint8_t rx[AppFrameSize];

	exchangeAppFrame(spi, AppCmdReadVersion, rx);
	chThdSleepMilliseconds(1);
	exchangeAppFrame(spi, AppCmdNop, rx);

	if (!isAppResponse(rx, AppCmdReadVersion, 4)) {
		efiPrintf(
				"G0 extension firmware: invalid version response %02X %02X %02X %02X",
				rx[0],
				rx[1],
				rx[2],
				rx[3]);
		return false;
	}

	version = readLe32(&rx[AppHeaderSize]);
	return true;
}

void requestAppUpdate(SPIDriver* spi) {
	uint8_t rx[AppFrameSize];

	exchangeAppFrame(spi, AppCmdEnterUpdate, rx);
	chThdSleepMilliseconds(1);
	exchangeAppFrame(spi, AppCmdNop, rx);
}

} // namespace g0_extension_firmware

#endif
