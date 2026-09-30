#include "log_field.h"
#include "buffered_writer.h"
#include "binary_log_header.h"

#include <gmock/gmock.h>
#include <vector>

using ::testing::_;
using ::testing::ElementsAre;
using ::testing::StrictMock;

class MockWriter : public Writer {
public:
	MOCK_METHOD(size_t, write, (const char* buffer, size_t count), (override));
	MOCK_METHOD(size_t, flush, (), (override));
};

TEST(BinaryLogHeader, DataOffsetLocatesFirstRecordAcross64KiBBoundary) {
	for (uint16_t count : {1, 736, 737, 768}) {
		SCOPED_TRACE(count);
		std::vector<uint8_t> file;
		StrictMock<MockWriter> writer;
		EXPECT_CALL(writer, write(_, _)).WillRepeatedly([&](const char* data, size_t size) {
			file.insert(file.end(), data, data + size);
			return size;
		});
		const uint32_t headerSize = 24u + count * 89u;
		writeBinaryLogFileHeader(writer, headerSize, count, count);
		uint8_t value = 0x5a;
		LogField field(value, "sample", "", 0);
		for (size_t i = 0; i < count; i++) {
			field.writeHeader(writer);
		}
		ASSERT_EQ(headerSize, file.size());
		const char blockHeader[] = {0, 7, 0x12, 0x34};
		writer.write(blockHeader, sizeof(blockHeader));
		uint8_t checksum = 0;
		for (size_t i = 0; i < count; i++) {
			char data;
			ASSERT_EQ(1, field.writeData(&data));
			writer.write(&data, 1);
			checksum += static_cast<uint8_t>(data);
		}
		writer.write(reinterpret_cast<const char*>(&checksum), 1);

		// Decode the documented big-endian widths independently of the writer.
		const uint32_t dataStart =
				(uint32_t(file[16]) << 24) | (uint32_t(file[17]) << 16) | (uint32_t(file[18]) << 8) | file[19];
		const uint16_t recordSize = (uint16_t(file[20]) << 8) | file[21];
		const uint16_t fieldCount = (uint16_t(file[22]) << 8) | file[23];
		EXPECT_EQ(count, fieldCount);
		EXPECT_EQ(count, recordSize);
		ASSERT_EQ(24u + fieldCount * 89u, dataStart);
		ASSERT_EQ(file.size(), dataStart + 4u + recordSize + 1u);
		EXPECT_EQ(0, file[dataStart]);
		EXPECT_EQ(7, file[dataStart + 1]);
		EXPECT_EQ(0x12, file[dataStart + 2]);
		EXPECT_EQ(0x34, file[dataStart + 3]);
		uint8_t decodedChecksum = 0;
		for (size_t i = 0; i < fieldCount; i++) {
			EXPECT_EQ(0x5a, file[dataStart + 4 + i]);
			decodedChecksum += file[dataStart + 4 + i];
		}
		EXPECT_EQ(decodedChecksum, file.back());
	}
}

TEST(BinaryLogHeader, Full32BitOffsetPreservesOtherHeaderFields) {
	StrictMock<MockWriter> writer;
	EXPECT_CALL(writer, write(_, 24)).WillOnce([](const char* data, size_t size) {
		const auto* bytes = reinterpret_cast<const uint8_t*>(data);
		EXPECT_EQ(0, memcmp(data, "MLVLG\0\0\2", 8));
		for (size_t i = 8; i < 16; i++) {
			EXPECT_EQ(0, bytes[i]);
		}
		EXPECT_EQ(0x12, bytes[16]);
		EXPECT_EQ(0x34, bytes[17]);
		EXPECT_EQ(0x56, bytes[18]);
		EXPECT_EQ(0x78, bytes[19]);
		EXPECT_EQ(0xab, bytes[20]);
		EXPECT_EQ(0xcd, bytes[21]);
		EXPECT_EQ(0x01, bytes[22]);
		EXPECT_EQ(0x02, bytes[23]);
		return size;
	});
	writeBinaryLogFileHeader(writer, 0x12345678, 0xabcd, 0x0102);
}

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
