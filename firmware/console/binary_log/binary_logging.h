/**
 * @file binary_logging.h
 */

#include <cstddef>
#include <cstdint>

struct Writer;
void writeFileHeader(Writer& buffer);
void writeSdLogLine(Writer& buffer);
void writeSdBlock(Writer& outBuffer);

// Number of logged fields, and the size of one record's field data. A full block on disk is
// the record length plus MLQ_BLOCK_OVERHEAD. Both scale with the output channel count, so the
// SD card logger reports them at startup to make the demanded data rate visible.
size_t getSdLogFieldCount();
uint16_t getSdLogRecordLength();

// Per-block overhead: block type, rolling counter, 2 byte timestamp, 1 byte checksum
#define MLQ_BLOCK_OVERHEAD 5
