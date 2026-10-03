/**
 * See also BinarySensorLog.java
 * See also mlq_file_format.txt
 */

#include "pch.h"

#include "binary_logging.h"
#include "binary_log_header.h"
#include "log_field.h"
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

// The list of logged fields lives in a separate file so it can eventually be tool-generated
#include "log_fields_generated.h"

static constexpr size_t computeFieldsRecordLength() {
	size_t recLength = 0;
	for (size_t i = 0; i < efi::size(fields); i++) {
		recLength += fields[i].getSize();
	}

	return recLength;
}

static uint64_t binaryLogCount = 0;

extern bool main_loop_started;

void writeSdLogLine(Writer& bufferedWriter) {
	if (!main_loop_started) {
		return;
	}

	if (binaryLogCount == 0) {
		writeFileHeader(bufferedWriter);
	} else {
		updateTunerStudioState();
		writeSdBlock(bufferedWriter);
	}

	binaryLogCount++;
}

static constexpr size_t recordLength = computeFieldsRecordLength();

static constexpr size_t headerSize = MLQ_HEADER_SIZE + efi::size(fields) * MLQ_FIELD_HEADER_SIZE;

static_assert(MLQ_HEADER_SIZE == 24, "MLG v2 requires a 24 byte file header");
static_assert(headerSize <= UINT32_MAX, "SD log header exceeds the 32 bit data begin index");
static_assert(recordLength <= UINT16_MAX, "SD log record length exceeds its 16 bit field");
static_assert(efi::size(fields) <= UINT16_MAX, "SD log field count exceeds its 16 bit field");

size_t getSdLogFieldCount() {
	return efi::size(fields);
}

uint16_t getSdLogRecordLength() {
	return recordLength;
}

void writeFileHeader(Writer& outBuffer) {
	writeBinaryLogFileHeader(outBuffer, headerSize, recordLength, efi::size(fields));

	// Write the actual logger fields, offset 24
	for (size_t i = 0; i < efi::size(fields); i++) {
		fields[i].writeHeader(outBuffer);
	}
}

static uint8_t blockRollCounter = 0;

void writeSdBlock(Writer& outBuffer) {
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

	// Sigh.
	*reinterpret_cast<uint32_t*>(&packedTime) = nowNt / TicksPerCount;

	// Snapshot the entire output channel space once, exactly like the main TunerStudio log does.
	// Offset-based fields read from this buffer; the timestamp field reads from its own address.
	static uint8_t channels[TS_TOTAL_OUTPUT_SIZE];
	copyRange(channels, getLiveDataFragments(), 0, TS_TOTAL_OUTPUT_SIZE);

	uint8_t sum = 0;
	for (size_t fieldIndex = 0; fieldIndex < efi::size(fields); fieldIndex++) {
		size_t entrySize = fields[fieldIndex].writeData(buffer, channels);

		for (size_t byteIndex = 0; byteIndex < entrySize; byteIndex++) {
			// "CRC" at the end is just the sum of all bytes
			sum += buffer[byteIndex];
		}
		outBuffer.write(buffer, entrySize);
	}

	buffer[0] = sum;
	// 1 byte checksum footer
	outBuffer.write(buffer, 1);
}

#endif /* EFI_FILE_LOGGING */
