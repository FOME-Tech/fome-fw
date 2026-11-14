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

bool Bootloader::command(uint8_t command, uint32_t timeoutMs) {
	const uint8_t packet[] = {0x5a, command, uint8_t(command ^ 0xff)};
	send(packet, sizeof(packet));
	return ack(timeoutMs);
}

bool Bootloader::connect() {
	m_connected = false;
	m_io.byte(0x5a);
	// An already synchronized ROM can NACK the sync byte; Get ID confirms the session.
	ack(100);
	if (!command(0x02, 100)) {
		return false;
	}
	m_io.byte(0); // Receive-data BUSY byte.
	if (m_io.byte(0) != 1) {
		return false;
	}
	uint16_t id = uint16_t(m_io.byte(0)) << 8;
	id |= m_io.byte(0);
	m_connected = ack(100) && id == 0x0460; // STM32G07x/08x, RM0444 option layout.
	return m_connected;
}

bool Bootloader::address(uint32_t address) {
	uint8_t packet[] = {uint8_t(address >> 24), uint8_t(address >> 16), uint8_t(address >> 8), uint8_t(address), 0};
	packet[4] = packet[0] ^ packet[1] ^ packet[2] ^ packet[3];
	send(packet, sizeof(packet));
	return ack();
}

bool Bootloader::write(uint32_t location, const uint8_t* data, size_t size) {
	if (size == 0 || size > 256 || size % 8 || !command(0x31) || !address(location)) {
		return false;
	}
	uint8_t checksum = uint8_t(size - 1);
	m_io.byte(checksum);
	for (size_t i = 0; i < size; i++) {
		m_io.byte(data[i]);
		checksum ^= data[i];
	}
	m_io.byte(checksum);
	if (location != OptionsBase) {
		m_io.sleepMs(10); // Allow main-flash programming to finish before polling.
	}
	return ack(location == OptionsBase ? 100 : 1000);
}

bool Bootloader::startRead(uint32_t location, size_t size) {
	if (size == 0 || size > 256 || !command(0x11) || !address(location)) {
		return false;
	}
	const uint8_t length[] = {uint8_t(size - 1), uint8_t((size - 1) ^ 0xff)};
	send(length, sizeof(length));
	if (!ack()) {
		return false;
	}
	m_io.byte(0); // Receive-data BUSY byte.
	return true;
}

bool Bootloader::read(uint32_t location, uint8_t* data, size_t size) {
	if (!startRead(location, size)) {
		return false;
	}
	for (size_t i = 0; i < size; i++) {
		data[i] = m_io.byte(0);
	}
	return true;
}

bool Bootloader::verify(uint32_t location, const uint8_t* data, size_t size) {
	if (!startRead(location, size)) {
		return false;
	}
	bool matches = true;
	for (size_t i = 0; i < size; i++) {
		if (m_io.byte(0) != data[i]) {
			matches = false;
		}
	}
	return matches;
}

bool Bootloader::enableBootPin() {
	uint8_t options[OptionsSize];
	if (!read(OptionsBase, options, sizeof(options)) || !verify(OptionsBase, options, sizeof(options))) {
		return false;
	}
	// RM0444: seven option pairs at +0x00..0x30 and SECR at +0x70; the rest is reserved.
	static constexpr uint8_t offsets[] = {0, 8, 16, 24, 32, 40, 48, 112};
	for (auto offset : offsets) {
		if ((little32(options + offset) ^ little32(options + offset + 4)) != 0xffffffffU) {
			return false;
		}
	}
	const uint32_t user = little32(options);
	const uint32_t nrstMode = (user >> 27) & 3;
	if ((user & 0xff) != 0xaa || !(user & (1U << 25)) || (little32(options + 112) & (1U << 16)) ||
		(nrstMode != 1 && nrstMode != 3)) {
		return false;
	}
	if (!(user & (1U << 24))) {
		return true;
	}
	options[3] &= ~1U; // nBOOT_SEL and its complemented copy; preserve every other option bit.
	options[7] |= 1U;
	// Reload can interrupt the final ACK; only reconnect and full readback establish success.
	write(OptionsBase, options, sizeof(options));
	m_connected = false;
	m_io.reconnect();
	return connect() && verify(OptionsBase, options, sizeof(options));
}

bool Bootloader::flash(const uint8_t* image, size_t size) {
	if (!validImage(image, size)) {
		return false;
	}
	if ((!m_connected && !connect()) || !enableBootPin() || !command(0x44)) {
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
		if (!write(FlashBase + offset, block, sizeof(block)) || !verify(FlashBase + offset, block, sizeof(block))) {
			return false;
		}
	}
	return true;
}

bool Bootloader::go() {
	return m_connected && command(0x21) && address(FlashBase);
}
} // namespace g0
