/**
 * @file binary_logging.h
 */

#include <cstddef>
#include <cstdint>

struct Writer;
void writeFileHeader(Writer& buffer);
void writeSdLogLine(Writer& buffer);
void writeSdBlock(Writer& outBuffer);

// Per-block overhead: block type, rolling counter, 2 byte timestamp, 1 byte checksum
#define MLQ_BLOCK_OVERHEAD 5
