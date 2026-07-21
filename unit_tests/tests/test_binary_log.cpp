#include "log_field.h"
#include "buffered_writer.h"

#include <gmock/gmock.h>

using ::testing::_;
using ::testing::ElementsAre;
using ::testing::StrictMock;

class MockWriter : public Writer {
public:
	MOCK_METHOD(size_t, write, (const char* buffer, size_t count), (override));
	MOCK_METHOD(size_t, flush, (), (override));
};

TEST(BinaryLogField, FieldHeader) {
	scaled_channel<int8_t, 10> channel;
	LogField field(channel, "name", "units", 2, "category");

	char buffer[89];
	StrictMock<MockWriter> bufWriter;
	EXPECT_CALL(bufWriter, write(_, 89)).WillOnce([&](const char* buf, size_t count) {
		memcpy(buffer, buf, count);
		return 0;
	});

	// Should write 89 bytes
	field.writeHeader(bufWriter);

	// Expect correctly written header
	EXPECT_THAT(
			buffer,
			ElementsAre(
					1, // type: int8_t
					// name - 34 bytes, 0 padded
					'n',
					'a',
					'm',
					'e',
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					// units - 10 bytes, 0 padded
					'u',
					'n',
					'i',
					't',
					's',
					0,
					0,
					0,
					0,
					0,
					// display style: float
					0,
					// Scale = 0.1 (float)
					0x3d,
					0xcc,
					0xcc,
					0xcd,
					// Transform - we always use 0
					0,
					0,
					0,
					0,
					// Digits - 2, as configured
					2,
					'c',
					'a',
					't',
					'e',
					'g',
					'o',
					'r',
					'y',
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0));
}

TEST(BinaryLogField, Value) {
	scaled_channel<uint32_t, 1> testValue = 12345678;
	LogField lf(testValue, "test", "unit", 0);

	char buffer[6];
	memset(buffer, 0xAA, sizeof(buffer));

	// Should write 4 bytes
	EXPECT_EQ(4, lf.writeData(buffer));

	// Check that big endian data was written, and bytes after weren't touched
	EXPECT_THAT(buffer, ElementsAre(0x00, 0xbc, 0x61, 0x4e, 0xAA, 0xAA));
}

TEST(BinaryLogField, OffsetValue) {
	// Offset-based field reads from a snapshot of the output channel space at its offset
	// (how generated SD log fields work), rather than from a fixed address.
	uint8_t channels[8] = {0};
	// uint16_t value 0x1234 stored little-endian (native) at offset 4
	channels[4] = 0x34;
	channels[5] = 0x12;

	LogField lf(uint16_t(4), LogField::Type::U16, 1, "test", "unit", 0);

	char buffer[4];
	// Sentinel is positive so gmock's char/int comparison is unambiguous
	memset(buffer, 0x7F, sizeof(buffer));

	// Should read 2 bytes at offset 4 and write them big-endian
	EXPECT_EQ(2, lf.writeData(buffer, channels));
	EXPECT_THAT(buffer, ElementsAre(0x12, 0x34, 0x7F, 0x7F));
}

TEST(BinaryLogField, BitValue) {
	// Single-bit field extracts one bit from the output channel snapshot, emitting it as a 0/1 byte.
	// Bit groups are little-endian words, so bit i lives in byte offset + i/8 at bit position i%8.
	uint8_t channels[8] = {0};
	// Word at offset 4: bits 1 and 9 set (byte 4 = 0b0000'0010, byte 5 = 0b0000'0010)
	channels[4] = 0x02;
	channels[5] = 0x02;

	char buffer[2];

	// Bit 1 is set -> writes a single 1 byte
	LogField bit1(uint16_t(4), uint8_t(1), "flag one");
	memset(buffer, 0x7F, sizeof(buffer));
	EXPECT_EQ(1, bit1.writeData(buffer, channels));
	EXPECT_THAT(buffer, ElementsAre(1, 0x7F));

	// Bit 0 is clear -> writes a single 0 byte
	LogField bit0(uint16_t(4), uint8_t(0), "flag zero");
	memset(buffer, 0x7F, sizeof(buffer));
	EXPECT_EQ(1, bit0.writeData(buffer, channels));
	EXPECT_THAT(buffer, ElementsAre(0, 0x7F));

	// Bit 9 (set) lives in the second byte of the word - exercises the byte-index math
	LogField bit9(uint16_t(4), uint8_t(9), "flag nine");
	memset(buffer, 0x7F, sizeof(buffer));
	EXPECT_EQ(1, bit9.writeData(buffer, channels));
	EXPECT_THAT(buffer, ElementsAre(1, 0x7F));
}

TEST(BinaryLogField, BitFieldHeader) {
	// A single-bit field is described as a U08 with the On/Off (4) display style.
	LogField field(uint16_t(0), uint8_t(3), "name", "category");

	char buffer[89];
	StrictMock<MockWriter> bufWriter;
	EXPECT_CALL(bufWriter, write(_, 89)).WillOnce([&](const char* buf, size_t count) {
		memcpy(buffer, buf, count);
		return 0;
	});

	field.writeHeader(bufWriter);

	// Offset 0: type U08 (0)
	EXPECT_EQ(0, buffer[0]);
	// Offset 45: display style On/Off (4)
	EXPECT_EQ(4, buffer[45]);
	// Offset 54: digits 0
	EXPECT_EQ(0, buffer[54]);
}
