/**
 * @file	tunerstudio_io.cpp
 *
 * @date Mar 8, 2015
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"

#include "tunerstudio_io.h"

#if EFI_SIMULATOR
#include "rusEfiFunctionalTest.h"
#endif // EFI_SIMULATOR

#if EFI_PROD_CODE || EFI_SIMULATOR
size_t TsChannelBase::read(uint8_t* buffer, size_t size) {
	return readTimeout(buffer, size, SR5_READ_TIMEOUT);
}
#endif

#define isBigPacket(size) ((size) > BLOCKING_FACTOR + 7)

void TsChannelBase::copyAndWriteSmallCrcPacket(const uint8_t* buf, size_t size) {
	// don't transmit too large a buffer
	efiAssertVoid(ObdCode::OBD_PCM_Processor_Fault, !isBigPacket(size), "copyAndWriteSmallCrcPacket tried to transmit too large a packet")

	// If transmitting data, copy it in to place in the scratch buffer
	// We want to prevent the data changing itself (higher priority threads could write
	// tsOutputChannels) during the CRC computation.  Instead compute the CRC on our
	// local buffer that nobody else will write.
	if (size) {
		memcpy(scratchBuffer, buf, size);
	}

	writeCrcPacketLocked(TS_RESPONSE_OK, &scratchBuffer[0], size);
}

void TsChannelBase::writeCrcPacketLocked(const uint8_t responseCode, const uint8_t* buf, const size_t size) {
	uint8_t headerBuffer[3];
	*(uint16_t*)headerBuffer = SWAP_UINT16(size + 1);
	*(uint8_t*)(headerBuffer + 2) = responseCode;
	// Write header
	write(headerBuffer, sizeof(headerBuffer), /*isEndOfPacket*/false);

	// If data, write that
	if (size) {
		write(buf, size, /*isEndOfPacket*/false);
	}

	// Command part of CRC
	uint32_t crc = crc32((void*)(headerBuffer + 2), 1);

	// Data part of CRC
	crc = crc32inc((void*)buf, crc, size);

	uint8_t crcBuffer[4];
	*(uint32_t*)crcBuffer = SWAP_UINT32(crc);

	// Lastly the CRC footer
	write(crcBuffer, sizeof(crcBuffer), /*isEndOfPacket*/true);
	flush();
}

TsChannelBase::TsChannelBase(const char *name)
	: m_name(name)
{
}
