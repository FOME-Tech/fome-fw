#pragma once

#include <stdint.h>

namespace g0_spi_protocol {

static constexpr uint8_t analogChannelCount = 12U;
static constexpr uint8_t digitalInputCount = 4U;
static constexpr uint8_t outputCount = 4U;
static constexpr uint16_t outputDutyMax = 10000U;

static constexpr uint8_t appFrameSize = 36U;
static constexpr uint8_t appHeaderSize = 4U;
static constexpr uint32_t protocolVersion = 9U;

static constexpr uint8_t versionPayloadLength = 8U;
static constexpr uint8_t analogPayloadLength = 26U;
static constexpr uint8_t digitalPayloadLength = 17U;
static constexpr uint8_t digitalAllPayloadLength = 25U;
static constexpr uint8_t sentPayloadLength = 9U;
static constexpr uint8_t outputPayloadLength = 7U;

static constexpr uint8_t cmdNop = 0x00U;
static constexpr uint8_t cmdReadVersion = 0x01U;
static constexpr uint8_t cmdReadAnalog = 0x10U;
static constexpr uint8_t cmdReadDigital = 0x11U;
static constexpr uint8_t cmdReadDigitalAll = 0x12U;
static constexpr uint8_t cmdReadSent = 0x13U;
static constexpr uint8_t cmdSetOutput = 0x20U;
static constexpr uint8_t cmdDisableOutput = 0x21U;
static constexpr uint8_t cmdReadOutput = 0x22U;
static constexpr uint8_t cmdSetInputMode = 0x30U;
static constexpr uint8_t cmdEnterUpdate = 0xA5U;

static constexpr uint8_t statusReady = 0x00U;
static constexpr uint8_t statusUpdateMode = 0x01U;

static constexpr uint8_t resultOk = 0x00U;
static constexpr uint8_t resultInvalid = 0x01U;
static constexpr uint8_t resultRange = 0x02U;
static constexpr uint8_t resultBusy = 0x03U;

static constexpr uint8_t digitalMode = 0x00U;

#if defined(__GNUC__)
#define G0_SPI_PROTOCOL_PACKED __attribute__((packed))
#else
#define G0_SPI_PROTOCOL_PACKED
#endif

struct G0_SPI_PROTOCOL_PACKED ResponseHeader {
	uint8_t status;
	uint8_t result;
	uint8_t command;
	uint8_t payloadLength;
};

struct G0_SPI_PROTOCOL_PACKED CommandRequest {
	uint8_t command;
};

struct G0_SPI_PROTOCOL_PACKED IndexedRequest {
	uint8_t command;
	uint8_t index;
};

struct G0_SPI_PROTOCOL_PACKED SetInputModeRequest {
	uint8_t command;
	uint8_t input;
	uint8_t mode;
};

struct G0_SPI_PROTOCOL_PACKED SetOutputRequest {
	uint8_t command;
	uint8_t output;
	uint32_t frequencyHz;
	uint16_t duty;
};

struct G0_SPI_PROTOCOL_PACKED VersionResponse {
	ResponseHeader header;
	uint32_t version;
	uint32_t protocolVersion;
};

struct G0_SPI_PROTOCOL_PACKED AnalogResponse {
	ResponseHeader header;
	uint8_t ready;
	uint8_t channelCount;
	uint16_t millivolts[analogChannelCount];
};

struct G0_SPI_PROTOCOL_PACKED DigitalResponse {
	ResponseHeader header;
	uint8_t input;
	uint8_t mode;
	uint8_t flags;
	uint32_t frequencyHz;
	uint16_t dutyPermille;
	uint16_t periodUs;
	uint16_t highTimeUs;
	uint32_t captureCount;
};

struct G0_SPI_PROTOCOL_PACKED DigitalAllState {
	uint8_t mode;
	uint8_t flags;
	uint16_t frequencyHz;
	uint16_t dutyPermille;
};

struct G0_SPI_PROTOCOL_PACKED DigitalAllResponse {
	ResponseHeader header;
	uint8_t inputCount;
	DigitalAllState inputs[digitalInputCount];
};

struct G0_SPI_PROTOCOL_PACKED SentResponse {
	ResponseHeader header;
	uint8_t input;
	uint32_t data;
	uint8_t status;
	uint8_t crc;
	uint8_t nibbleCount;
	uint8_t flags;
};

struct G0_SPI_PROTOCOL_PACKED OutputResponse {
	ResponseHeader header;
	uint8_t output;
	uint32_t frequencyHz;
	uint16_t duty;
};

union AppFrame {
	uint8_t bytes[appFrameSize];
	CommandRequest commandRequest;
	IndexedRequest indexedRequest;
	SetInputModeRequest setInputModeRequest;
	SetOutputRequest setOutputRequest;
	ResponseHeader responseHeader;
	VersionResponse versionResponse;
	AnalogResponse analogResponse;
	DigitalResponse digitalResponse;
	DigitalAllResponse digitalAllResponse;
	SentResponse sentResponse;
	OutputResponse outputResponse;
};

inline uint16_t crc16(const uint8_t* bytes, uint8_t size) {
	uint16_t crc = 0xFFFFU;
	for (uint8_t i = 0; i < size; i++) {
		crc ^= static_cast<uint16_t>(bytes[i]) << 8U;
		for (uint8_t bit = 0; bit < 8U; bit++) {
			crc = (crc & 0x8000U) ? (crc << 1U) ^ 0x1021U : crc << 1U;
		}
	}
	return crc;
}

inline void seal(AppFrame& frame) {
	const uint16_t crc = crc16(frame.bytes, appFrameSize - 2U);
	frame.bytes[appFrameSize - 2U] = crc & 0xFFU;
	frame.bytes[appFrameSize - 1U] = crc >> 8U;
}

inline bool validFrame(const AppFrame& frame) {
	return crc16(frame.bytes, appFrameSize - 2U) ==
		   (frame.bytes[appFrameSize - 2U] | (frame.bytes[appFrameSize - 1U] << 8U));
}

static_assert(sizeof(ResponseHeader) == appHeaderSize, "Unexpected G0 SPI response header size");
static_assert(sizeof(VersionResponse) - appHeaderSize == versionPayloadLength, "Unexpected G0 version payload size");
static_assert(sizeof(AnalogResponse) - appHeaderSize == analogPayloadLength, "Unexpected G0 analog payload size");
static_assert(sizeof(DigitalResponse) - appHeaderSize == digitalPayloadLength, "Unexpected G0 digital payload size");
static_assert(
		sizeof(DigitalAllResponse) - appHeaderSize == digitalAllPayloadLength,
		"Unexpected G0 digital-all payload size");
static_assert(sizeof(SentResponse) - appHeaderSize == sentPayloadLength, "Unexpected G0 SENT payload size");
static_assert(sizeof(OutputResponse) - appHeaderSize == outputPayloadLength, "Unexpected G0 output payload size");
static_assert(sizeof(AppFrame) == appFrameSize, "Unexpected G0 SPI app frame size");

#undef G0_SPI_PROTOCOL_PACKED

} // namespace g0_spi_protocol
