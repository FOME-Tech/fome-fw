/**
 * See also BinarySensorLog.java
 * See also mlq_file_format.txt
 */

#include "pch.h"

#include "binary_logging.h"
#include "buffered_writer.h"
#include "tunerstudio.h"

#if EFI_FILE_LOGGING

// 2^32 milliseconds is 49 days, this is plenty of time.
constexpr int TimestampCountsPerSec = 1000;
constexpr int TicksPerCount = (US_TO_NT_MULTIPLIER * 1000000) / TimestampCountsPerSec;

// Check that it's an integer number of ticks
static_assert(US_TO_NT_MULTIPLIER * 1000000 == TimestampCountsPerSec * TicksPerCount);

static scaled_channel<uint32_t, TimestampCountsPerSec> packedTime;

#include "sd_log_header.h"

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
	return;

	static char buffer[16];

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

	// // Sigh.
	// *reinterpret_cast<uint32_t*>(&packedTime) = nowNt / TicksPerCount;

	uint8_t sum = 0;
	// for (size_t fieldIndex = 0; fieldIndex < efi::size(fields); fieldIndex++) {
	// 	size_t entrySize = fields[fieldIndex].writeData(buffer);

	// 	for (size_t byteIndex = 0; byteIndex < entrySize; byteIndex++) {
	// 		// "CRC" at the end is just the sum of all bytes
	// 		sum += buffer[byteIndex];
	// 	}
	// 	outBuffer.write(buffer, entrySize);
	// }

	buffer[0] = sum;
	// 1 byte checksum footer
	outBuffer.write(buffer, 1);
}

#endif /* EFI_FILE_LOGGING */
