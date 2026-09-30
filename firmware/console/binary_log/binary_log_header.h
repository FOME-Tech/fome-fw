#pragma once

#include "buffered_writer.h"

#include <cstdint>
#include <cstring>

// MLG v2 uses a big-endian 32-bit data offset and 16-bit record length/count.
// https://www.efianalytics.com/TunerStudio/docs/MLG_Binary_LogFormat_2.0.pdf
inline void
writeBinaryLogFileHeader(Writer& writer, uint32_t dataBeginIndex, uint16_t recordLength, uint16_t fieldsCount) {
	uint8_t buffer[24] = {};
	memcpy(buffer, "MLVLG", 6);
	buffer[7] = 2;
	// Timestamp and optional info-data offset are zero when absent.
	buffer[16] = dataBeginIndex >> 24;
	buffer[17] = dataBeginIndex >> 16;
	buffer[18] = dataBeginIndex >> 8;
	buffer[19] = dataBeginIndex;
	buffer[20] = recordLength >> 8;
	buffer[21] = recordLength;
	buffer[22] = fieldsCount >> 8;
	buffer[23] = fieldsCount;
	writer.write(reinterpret_cast<const char*>(buffer), sizeof(buffer));
}
