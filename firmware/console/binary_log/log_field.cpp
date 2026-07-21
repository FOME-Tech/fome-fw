#include "log_field.h"
#include "buffered_writer.h"

#include <cstring>

static void memcpy_swapend(void* dest, const void* src, size_t num) {
	const char* src2 = reinterpret_cast<const char*>(src);
	char* dest2 = reinterpret_cast<char*>(dest);

	for (size_t i = 0; i < num; i++) {
		// Endian swap - copy the end to the beginning
		dest2[i] = src2[num - 1 - i];
	}
}

static void copyFloat(char* buffer, float value) {
	memcpy_swapend(buffer, reinterpret_cast<char*>(&value), sizeof(float));
}

void LogField::writeHeader(Writer& outBuffer) const {
	char buffer[MLQ_FIELD_HEADER_SIZE];

	// Offset 0, length 1 = type
	buffer[0] = static_cast<char>(m_type);

	// Offset 1, length 34 = name
	strncpy(&buffer[1], m_name, 34);

	// Offset 35, length 10 = units
	strncpy(&buffer[35], m_units, 10);

	// Offset 45, length 1 = Display style
	// value 0 -> floating point number, value 4 -> On/Off (used for single-bit fields)
	buffer[45] = (m_bitIndex >= 0) ? 4 : 0;

	// Offset 46, length 4 = Scale
	copyFloat(buffer + 46, m_multiplier);

	// Offset 50, length 4 = shift before scaling (always 0)
	copyFloat(buffer + 50, 0);

	// Offset 54, size 1 = digits to display (signed int)
	buffer[54] = m_digits;

	// Offset 55, (optional) category string
	if (m_category) {
		size_t categoryLength = strlen(m_category);
		size_t lengthAfterCategory = 34 - categoryLength;
		memcpy(&buffer[55], m_category, categoryLength);
		memset(&buffer[55] + categoryLength, 0, lengthAfterCategory);
	} else {
		memset(&buffer[55], 0, 34);
	}

	// Total size = 89
	outBuffer.write(buffer, MLQ_FIELD_HEADER_SIZE);
}

size_t LogField::writeData(char* buffer, const uint8_t* channels) const {
	// Single-bit fields: extract one bit from the output-channel snapshot and emit it as a 0/1 byte.
	// Bit groups are little-endian words in the (little-endian ECU) output space, so bit i lives in
	// byte offset + i/8 at bit position i%8 - no endian swap needed for a single byte.
	if (m_bitIndex >= 0) {
		uint8_t byte = channels[m_offset + (m_bitIndex >> 3)];
		buffer[0] = (byte >> (m_bitIndex & 7)) & 1;
		return 1;
	}

	size_t size = m_size;

	// Pointer-based fields read from their fixed address; offset-based fields read from the
	// provided snapshot of the output channel space.
	const void* src = m_addr ? m_addr : (channels + m_offset);
	memcpy_swapend(buffer, src, size);

	return size;
}
