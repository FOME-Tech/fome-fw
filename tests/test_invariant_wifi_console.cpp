#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdint>
#include <algorithm>

// Simulated WiFi console buffer management class
// This mirrors the vulnerable pattern from wifi_console.cpp
class WiFiConsoleBuffer {
public:
    static constexpr size_t BUFFER_CAPACITY = 1024;

    WiFiConsoleBuffer() : m_writeSize(0) {
        memset(m_writeBuffer, 0, sizeof(m_writeBuffer));
        // Canary values to detect overflow
        memset(m_canary, 0xAB, sizeof(m_canary));
    }

    // Secure version: must validate before copying
    bool writeChunk(const uint8_t* buffer, size_t chunkSize) {
        if (buffer == nullptr) {
            return false;
        }
        // SECURITY INVARIANT: m_writeSize + chunkSize must never exceed BUFFER_CAPACITY
        if (chunkSize > BUFFER_CAPACITY || m_writeSize > BUFFER_CAPACITY - chunkSize) {
            return false; // Reject oversized writes
        }
        memcpy(&m_writeBuffer[m_writeSize], buffer, chunkSize);
        m_writeSize += chunkSize;
        return true;
    }

    size_t getWriteSize() const { return m_writeSize; }
    size_t getCapacity() const { return BUFFER_CAPACITY; }

    bool canaryIntact() const {
        for (size_t i = 0; i < sizeof(m_canary); i++) {
            if (m_canary[i] != 0xAB) return false;
        }
        return true;
    }

    void reset() {
        m_writeSize = 0;
        memset(m_writeBuffer, 0, sizeof(m_writeBuffer));
    }

private:
    uint8_t m_writeBuffer[BUFFER_CAPACITY];
    size_t  m_writeSize;
    uint8_t m_canary[64]; // Overflow detection sentinel
};

class SecurityTest : public ::testing::TestWithParam<std::string> {};

TEST_P(SecurityTest, BufferBoundaryNeverExceeded) {
    // Invariant: m_writeSize must never exceed BUFFER_CAPACITY regardless of input,
    // and memory beyond the buffer must never be corrupted by incoming network data.
    std::string payload = GetParam();

    WiFiConsoleBuffer console;

    const uint8_t* data = reinterpret_cast<const uint8_t*>(payload.data());
    size_t dataLen = payload.size();

    // Attempt to write the adversarial payload
    console.writeChunk(data, dataLen);

    // INVARIANT 1: Write pointer must never exceed buffer capacity
    EXPECT_LE(console.getWriteSize(), console.getCapacity())
        << "SECURITY VIOLATION: write size exceeded buffer capacity! "
        << "writeSize=" << console.getWriteSize()
        << " capacity=" << console.getCapacity()
        << " payload_size=" << dataLen;

    // INVARIANT 2: Canary bytes must remain intact (no overflow into adjacent memory)
    EXPECT_TRUE(console.canaryIntact())
        << "SECURITY VIOLATION: buffer overflow detected via canary corruption! "
        << "payload_size=" << dataLen;
}

TEST_P(SecurityTest, MultipleChunksNeverExceedCapacity) {
    // Invariant: Repeated writes with adversarial chunks must never overflow the buffer.
    std::string payload = GetParam();

    WiFiConsoleBuffer console;

    const uint8_t* data = reinterpret_cast<const uint8_t*>(payload.data());
    size_t dataLen = payload.size();

    // Simulate multiple network chunks arriving
    size_t chunkSize = std::max<size_t>(1, dataLen / 4);
    size_t offset = 0;

    while (offset < dataLen) {
        size_t remaining = dataLen - offset;
        size_t toWrite = std::min(chunkSize, remaining);
        console.writeChunk(data + offset, toWrite);

        // INVARIANT: After every chunk write, size must not exceed capacity
        EXPECT_LE(console.getWriteSize(), console.getCapacity())
            << "SECURITY VIOLATION: write size exceeded buffer capacity after chunk write! "
            << "writeSize=" << console.getWriteSize()
            << " capacity=" << console.getCapacity()
            << " chunk_offset=" << offset
            << " chunk_size=" << toWrite;

        // INVARIANT: Canary must remain intact after every write
        EXPECT_TRUE(console.canaryIntact())
            << "SECURITY VIOLATION: buffer overflow detected via canary after chunk write! "
            << "chunk_offset=" << offset;

        offset += toWrite;
    }
}

TEST_P(SecurityTest, NearBoundaryFillThenOverflow) {
    // Invariant: Filling buffer to near-capacity then writing adversarial payload
    // must never cause the write pointer to exceed buffer bounds.
    std::string payload = GetParam();

    WiFiConsoleBuffer console;

    // Fill buffer to near capacity (leave 1 byte)
    std::vector<uint8_t> filler(WiFiConsoleBuffer::BUFFER_CAPACITY - 1, 0x41);
    console.writeChunk(filler.data(), filler.size());

    ASSERT_LE(console.getWriteSize(), console.getCapacity())
        << "Pre-condition failed: filler write exceeded capacity";

    // Now attempt to write adversarial payload on top of near-full buffer
    const uint8_t* data = reinterpret_cast<const uint8_t*>(payload.data());
    size_t dataLen = payload.size();

    console.writeChunk(data, dataLen);

    // INVARIANT: Must never exceed capacity
    EXPECT_LE(console.getWriteSize(), console.getCapacity())
        << "SECURITY VIOLATION: overflow after near-boundary fill + adversarial write! "
        << "writeSize=" << console.getWriteSize()
        << " capacity=" << console.getCapacity()
        << " adversarial_payload_size=" << dataLen;

    // INVARIANT: Canary must remain intact
    EXPECT_TRUE(console.canaryIntact())
        << "SECURITY VIOLATION: canary corrupted after near-boundary fill + adversarial write! "
        << "adversarial_payload_size=" << dataLen;
}

TEST_P(SecurityTest, ZeroAndMaxSizeEdgeCases) {
    // Invariant: Edge case sizes (0, 1, max, max+1) must all be handled safely.
    std::string payload = GetParam();

    WiFiConsoleBuffer console;

    // Test zero-length write
    console.writeChunk(nullptr, 0);
    EXPECT_EQ(console.getWriteSize(), 0u)
        << "Zero-length null write should not advance write pointer";
    EXPECT_TRUE(console.canaryIntact());

    // Test exact capacity write
    console.reset();
    std::vector<uint8_t> exactFill(WiFiConsoleBuffer::BUFFER_CAPACITY, 0x42);
    bool result = console.writeChunk(exactFill.data(), exactFill.size());
    EXPECT_LE(console.getWriteSize(), console.getCapacity())
        << "SECURITY VIOLATION: exact-capacity write exceeded buffer!";
    EXPECT_TRUE(console.canaryIntact())
        << "SECURITY VIOLATION: canary corrupted by exact-capacity write!";

    // Test capacity+1 write (must be rejected)
    console.reset();
    std::vector<uint8_t> overFill(WiFiConsoleBuffer::BUFFER_CAPACITY + 1, 0x43);
    result = console.writeChunk(overFill.data(), overFill.size());
    EXPECT_FALSE(result)
        << "SECURITY VIOLATION: over-capacity write was accepted!";
    EXPECT_LE(console.getWriteSize(), console.getCapacity())
        << "SECURITY VIOLATION: write size exceeded capacity after over-capacity attempt!";
    EXPECT_TRUE(console.canaryIntact())
        << "SECURITY VIOLATION: canary corrupted by over-capacity write attempt!";
}

INSTANTIATE_TEST_SUITE_P(
    AdversarialInputs,
    SecurityTest,
    ::testing::Values(
        // Empty input
        std::string(""),
        // Single byte
        std::string(1, '\x41'),
        // Exact buffer capacity
        std::string(1024, '\xFF'),
        // One byte over capacity
        std::string(1025, '\xAA'),
        // Two times capacity
        std::string(2048, '\xBB'),
        // Maximum typical network packet (MTU)
        std::string(1500, '\xCC'),
        // Large chunk simulating fragmented attack
        std::string(4096, '\xDE'),
        // Very large payload (remote attacker sending huge data)
        std::string(65535, '\xAD'),
        // Null bytes (potential string termination bypass)
        std::string(1024, '\x00'),
        // Mixed adversarial pattern
        std::string(2000, '\x90'), // NOP sled pattern
        // Format string-like payload
        std::string("%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s"),
        // Boundary: capacity - 1
        std::string(1023, '\x41'),
        // Boundary: capacity + 100
        std::string(1124, '\x41'),
        // Repeated overflow attempt pattern
        std::string(8192, '\xFF'),
        // Unicode-like multi-byte sequences
        std::string(1024, '\xC0'),
        // All 0xFF bytes at double capacity
        std::string(2048, '\xFF')
    )
);

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}