#include "g0_extension_firmware_impl.h"

#if HW_ATLAS && HAL_USE_SPI && G0_EXTENSION_FIRMWARE_IMAGE_AVAILABLE

namespace g0_extension_firmware {

void exchangeAppFrame(SPIDriver* spi, uint8_t command, app::AppFrame& rx) {
	memset(txBuffer(), 0, app::appFrameSize);
	reinterpret_cast<app::AppFrame*>(txBuffer())->commandRequest.command = command;
	memset(rxBuffer(), 0, app::appFrameSize);
	memset(rx.bytes, 0, app::appFrameSize);

	spiSelect(spi);
	spiExchange(spi, app::appFrameSize, txBuffer(), rxBuffer());
	spiUnselect(spi);

	memcpy(rx.bytes, rxBuffer(), app::appFrameSize);
}

bool isAppResponse(const app::AppFrame& rx, uint8_t expectedCommand, uint8_t expectedPayloadLength) {
	const auto& header = rx.responseHeader;
	const bool knownStatus = header.status == app::statusReady || header.status == app::statusUpdateMode;
	return knownStatus && header.result == app::resultOk && header.command == expectedCommand &&
		   header.payloadLength == expectedPayloadLength;
}

bool readAppVersion(SPIDriver* spi, uint32_t& version) {
	app::AppFrame rx;

	exchangeAppFrame(spi, app::cmdReadVersion, rx);
	chThdSleepMilliseconds(1);
	exchangeAppFrame(spi, app::cmdNop, rx);

	if (!isAppResponse(rx, app::cmdReadVersion, app::versionPayloadLength)) {
		efiPrintf(
				"G0 extension firmware: invalid version response %02X %02X %02X %02X",
				rx.responseHeader.status,
				rx.responseHeader.result,
				rx.responseHeader.command,
				rx.responseHeader.payloadLength);
		return false;
	}

	version = rx.versionResponse.version;
	return true;
}

void requestAppUpdate(SPIDriver* spi) {
	app::AppFrame rx;

	exchangeAppFrame(spi, app::cmdEnterUpdate, rx);
	chThdSleepMilliseconds(1);
	exchangeAppFrame(spi, app::cmdNop, rx);
}

} // namespace g0_extension_firmware

#endif
