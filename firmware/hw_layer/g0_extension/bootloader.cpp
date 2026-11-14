#include "bootloader.h"
#include <cstring>

namespace g0 {
namespace {
uint32_t little32(const uint8_t* p) {
	return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
} // namespace

bool Bootloader::validImage(const uint8_t* image, size_t size) {
	if (!image || size < 8 || size > FlashSize) {
		return false;
	}
	const uint32_t sp = little32(image);
	const uint32_t entry = little32(image + 4);
	// STM32G071xB: 36 KiB SRAM, 128 KiB flash. Vector must point into this image.
	return sp > 0x20000000 && sp <= 0x20009000 && !(sp & 7) && (entry & 1) && (entry & ~1U) >= FlashBase + 8 &&
		   (entry & ~1U) < FlashBase + size;
}

void Bootloader::send(const uint8_t* data, size_t size) {
	// Outgoing data can equal ACK/NACK. Ignore MISO until the entire packet is sent.
	for (size_t i = 0; i < size; i++) {
		m_io.byte(data[i]);
	}
}

bool Bootloader::ack(uint32_t timeoutMs) {
	m_io.byte(0); // BUSY byte precedes every receive phase.
	for (uint32_t i = 0; i < timeoutMs; i++) {
		const auto value = m_io.byte(0);
		if (value == 0x79) {
			m_io.byte(0x79);
			return true;
		}
		if (value == 0x1f) {
			return false;
		}
		m_io.sleepMs(1);
	}
	return false;
}

bool Bootloader::command(uint8_t command) {
	const uint8_t packet[] = {0x5a, command, uint8_t(command ^ 0xff)};
	send(packet, sizeof(packet));
	return ack();
}

bool Bootloader::address(uint32_t address) {
	uint8_t packet[] = {uint8_t(address >> 24), uint8_t(address >> 16), uint8_t(address >> 8), uint8_t(address), 0};
	packet[4] = packet[0] ^ packet[1] ^ packet[2] ^ packet[3];
	send(packet, sizeof(packet));
	return ack();
}

bool Bootloader::write(uint32_t location, const uint8_t* data) {
	if (!command(0x31) || !address(location)) {
		return false;
	}
	uint8_t checksum = 255;
	m_io.byte(255);
	for (size_t i = 0; i < 256; i++) {
		m_io.byte(data[i]);
		checksum ^= data[i];
	}
	m_io.byte(checksum);
	m_io.sleepMs(10); // Allow programming to finish before polling.
	return ack();
}

bool Bootloader::verify(uint32_t location, const uint8_t* data) {
	if (!command(0x11) || !address(location)) {
		return false;
	}
	const uint8_t length[] = {255, 0};
	send(length, sizeof(length));
	if (!ack()) {
		return false;
	}
	m_io.byte(0); // Receive-data BUSY byte.
	bool matches = true;
	for (size_t i = 0; i < 256; i++) {
		if (m_io.byte(0) != data[i]) {
			matches = false;
		}
	}
	return matches;
}

bool Bootloader::flash(const uint8_t* image, size_t size) {
	if (!validImage(image, size)) {
		return false;
	}
	m_io.byte(0x5a);
	if (!ack() || !command(0x44)) {
		return false;
	}
	const uint8_t erase[] = {255, 255, 0};
	send(erase, sizeof(erase));
	if (!ack(10000)) {
		return false;
	}
	for (size_t offset = 0; offset < size; offset += 256) {
		uint8_t block[256];
		std::memset(block, 255, sizeof(block));
		const size_t count = size - offset < 256 ? size - offset : 256;
		std::memcpy(block, image + offset, count);
		if (!write(FlashBase + offset, block) || !verify(FlashBase + offset, block)) {
			return false;
		}
	}
	// The board resets into the application after deselecting SPI.
	return true;
}
} // namespace g0
