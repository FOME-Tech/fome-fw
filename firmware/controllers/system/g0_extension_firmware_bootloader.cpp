#include "g0_extension_firmware_impl.h"

#if HW_ATLAS && HAL_USE_SPI && G0_EXTENSION_FIRMWARE_IMAGE_AVAILABLE

namespace g0_extension_firmware {

uint8_t spiByte(SPIDriver* spi, uint8_t tx) {
	txBuffer()[0] = tx;
	rxBuffer()[0] = 0;
	spiExchange(spi, 1, txBuffer(), rxBuffer());
	return rxBuffer()[0];
}

uint8_t bootloaderSpiByte(SPIDriver* spi, uint8_t tx) {
	const uint8_t rx = spiByte(spi, tx);

	// AN4286 requires at least 15us between bytes for the STM32 ROM SPI bootloader.
	chThdSleepMicroseconds(BootloaderInterByteDelayUs);
	return rx;
}

bool handleBootloaderAck(SPIDriver* spi, uint8_t response) {
	if (response == BootloaderAck) {
		bootloaderSpiByte(spi, BootloaderAck);
		return true;
	}

	return false;
}

bool waitBootloaderAck(SPIDriver* spi, int attempts) {
	while (attempts-- > 0) {
		const uint8_t response = bootloaderSpiByte(spi, 0x00);

		if (handleBootloaderAck(spi, response)) {
			return true;
		}

		if (response == BootloaderNack) {
			return false;
		}

		chThdSleepMilliseconds(1);
	}

	return false;
}

bool sendBootloaderBytesAndWaitAck(SPIDriver* spi, const uint8_t* data, size_t size) {
	for (size_t i = 0; i < size; i++) {
		const uint8_t response = bootloaderSpiByte(spi, data[i]);

		if (handleBootloaderAck(spi, response)) {
			return true;
		}

		if (response == BootloaderNack) {
			return false;
		}
	}

	return waitBootloaderAck(spi);
}

bool sendBootloaderCommand(SPIDriver* spi, uint8_t command) {
	const uint8_t bytes[] = {BootloaderSync, command, static_cast<uint8_t>(command ^ 0xFF)};
	return sendBootloaderBytesAndWaitAck(spi, bytes, sizeof(bytes));
}

bool sendBootloaderAddress(SPIDriver* spi, uint32_t address) {
	uint8_t bytes[5] = {
			static_cast<uint8_t>(address >> 24),
			static_cast<uint8_t>(address >> 16),
			static_cast<uint8_t>(address >> 8),
			static_cast<uint8_t>(address),
			0};

	bytes[4] = bytes[0] ^ bytes[1] ^ bytes[2] ^ bytes[3];
	return sendBootloaderBytesAndWaitAck(spi, bytes, sizeof(bytes));
}

bool bootloaderMassErase(SPIDriver* spi) {
	if (!sendBootloaderCommand(spi, BootloaderExtendedErase)) {
		return false;
	}

	const uint8_t globalMassErase[] = {0xFF, 0xFF, 0x00};
	return sendBootloaderBytesAndWaitAck(spi, globalMassErase, sizeof(globalMassErase));
}

bool bootloaderWriteBlock(SPIDriver* spi, uint32_t address, const uint8_t* data, size_t size) {
	if (size == 0 || size > 256) {
		return false;
	}

	if (!sendBootloaderCommand(spi, BootloaderWriteMemory) || !sendBootloaderAddress(spi, address)) {
		return false;
	}

	uint8_t buffer[258];
	buffer[0] = static_cast<uint8_t>(size - 1);
	uint8_t checksum = buffer[0];

	for (size_t i = 0; i < size; i++) {
		buffer[1 + i] = data[i];
		checksum ^= data[i];
	}

	buffer[1 + size] = checksum;
	return sendBootloaderBytesAndWaitAck(spi, buffer, size + 2);
}

bool bootloaderGo(SPIDriver* spi, uint32_t address) {
	return sendBootloaderCommand(spi, BootloaderGo) && sendBootloaderAddress(spi, address);
}

bool flashFirmwareImage(SPIDriver* spi) {
	const size_t totalSize = sizeof(build_g0_extension_bin);

	if (totalSize == 0) {
		efiPrintf("G0 extension firmware ERROR: firmware image header is missing or empty");
		return false;
	}

	efiPrintf("G0 extension firmware: entering SPI bootloader");
	resetExtension(true);

	spiSelect(spi);
	bootloaderSpiByte(spi, BootloaderSync);
	if (!waitBootloaderAck(spi)) {
		spiUnselect(spi);
		efiPrintf("G0 extension firmware ERROR: no ACK from SPI bootloader");
		return false;
	}

	efiPrintf("G0 extension firmware: erasing flash");
	if (!bootloaderMassErase(spi)) {
		spiUnselect(spi);
		efiPrintf("G0 extension firmware ERROR: flash erase failed");
		return false;
	}

	efiPrintf("G0 extension firmware: writing %d bytes", static_cast<int>(totalSize));
	for (size_t offset = 0; offset < totalSize; offset += 256) {
		uint8_t block[256];
		const size_t blockSize = minI(256, totalSize - offset);
		memset(block, 0xFF, sizeof(block));
		memcpy(block, build_g0_extension_bin + offset, blockSize);

		if (!bootloaderWriteBlock(spi, FlashBase + offset, block, sizeof(block))) {
			spiUnselect(spi);
			efiPrintf("G0 extension firmware ERROR: write failed at offset %d", static_cast<int>(offset));
			return false;
		}
	}

	efiPrintf("G0 extension firmware: starting firmware");
	const bool ok = bootloaderGo(spi, FlashBase);
	spiUnselect(spi);

	resetExtension(false);
	return ok;
}

bool flashFirmwareImageWithRetry(SPIDriver* spi) {
	for (int attempt = 1; attempt <= FlashMaxAttempts; attempt++) {
		efiPrintf("G0 extension firmware: flash attempt %d/%d", attempt, FlashMaxAttempts);

		if (flashFirmwareImage(spi)) {
			return true;
		}

		resetExtension(false);
	}

	efiPrintf("G0 extension firmware ERROR: aborting G0 flash after %d failed attempts", FlashMaxAttempts);
	return false;
}

} // namespace g0_extension_firmware

#endif
