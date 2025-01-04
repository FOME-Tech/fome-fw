/**
 * See also BinarySensorLog.java
 * See also mlq_file_format.txt
 */

#include "pch.h"

#include "binary_logging.h"
#include "buffered_writer.h"
#include "tunerstudio.h"
#include "live_data.h"

#if EFI_FILE_LOGGING

// 2^32 milliseconds is 49 days, this is plenty of time.
constexpr int TimestampCountsPerSec = 1000;
constexpr int TicksPerCount = (US_TO_NT_MULTIPLIER * 1000000) / TimestampCountsPerSec;

// Check that it's an integer number of ticks
static_assert(US_TO_NT_MULTIPLIER * 1000000 == TimestampCountsPerSec * TicksPerCount);

static scaled_channel<uint32_t, TimestampCountsPerSec> packedTime;

template <typename T>
inline void sdWrite1(Writer& writer, const T& obj) {
	static_assert(sizeof(T) == 1);
	uint8_t x = *(reinterpret_cast<const uint8_t*>(&obj));
	writer.write((const char*)&x, 1);
}

template <typename T>
inline void sdWrite2(Writer& writer, const T& obj) {
	static_assert(sizeof(T) == 2);
	uint16_t x = SWAP_UINT16(*(reinterpret_cast<const uint16_t*>(&obj)));
	writer.write((const char*)&x, 2);
}

template <typename T>
inline void sdWrite4(Writer& writer, const T& obj) {
	static_assert(sizeof(T) == 4);
	uint8_t x = SWAP_UINT32(*(reinterpret_cast<const uint32_t*>(&obj)));
	writer.write((const char*)&x, 4);
}

static uint64_t binaryLogCount = 0;

extern bool main_loop_started;

void writeSdLogLine(Writer& bufferedWriter) {
	if (!main_loop_started)
		return;

	if (binaryLogCount == 0) {
		writeFileHeader(bufferedWriter);
	} else {
		updateTunerStudioState();
		writeSdBlock(bufferedWriter);
	}

	binaryLogCount++;
}

static const char bufferOfZeroes[64] = {0};

void writeFileHeader(Writer& outBuffer) {
	outBuffer.write(reinterpret_cast<const char*>(binaryLogHeader), sizeof(binaryLogHeader));

	// Pad out to 64k as that's where we said the data starts
	int remain = 65536 - sizeof(binaryLogHeader);

	efiAssertVoid(ObdCode::OBD_PCM_Processor_Fault, remain > 0, "Invalid SD header");

	while (remain > sizeof(bufferOfZeroes)) {
		outBuffer.write(bufferOfZeroes, sizeof(bufferOfZeroes));
		remain = remain - sizeof(bufferOfZeroes);
	}

	if (remain) {
		outBuffer.write(bufferOfZeroes, remain);
	}
}

static uint8_t blockRollCounter = 0;

void writeSdBlock(Writer& outBuffer) {
	static char buffer[2048];

	// Offset 0 = Block type, standard data block in this case
	buffer[0] = 0;

	// Offset 1 = rolling counter sequence number
	buffer[1] = blockRollCounter++;

	auto nowNt = getTimeNowNt();

	// Offset 2, size 2 = Timestamp at 10us resolution
	uint16_t timestamp = (nowNt / (US_TO_NT_MULTIPLIER * 10));
	buffer[2] = timestamp >> 8;
	buffer[3] = timestamp & 0xFF;

	outBuffer.write(buffer, 4);

	memcpy(buffer, getLiveData<wall_fuel_state_s>({}), sizeof(wall_fuel_state_s));

	// // Sigh.
	// *reinterpret_cast<uint32_t*>(&packedTime) = nowNt / TicksPerCount;

	uint8_t sum = 0;
	const auto fragments = getLiveDataFragments();

	for (size_t i = 0; i < fragments.count; i++) {
		const auto& fragment = fragments.fragments[i];

		memcpy(buffer, fragment.get(), fragment.size);

		for (size_t j = 0; j < fragment.size; j++) {
			sum += buffer[j];
		}

		outBuffer.write(buffer, fragment.size);
	}

	buffer[0] = sum;
	// 1 byte checksum footer
	outBuffer.write(buffer, 1);
}

#endif /* EFI_FILE_LOGGING */
