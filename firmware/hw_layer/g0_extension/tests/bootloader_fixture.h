#pragma once
#include "../bootloader.h"
#include <array>
#include <cassert>
#include <utility>
#include <vector>

namespace boot_test {
using Options = std::array<uint8_t, 128>;
constexpr uint32_t optionsAddress = 0x1fff7800;

inline void word(uint8_t* data, uint32_t value) {
	for (unsigned i = 0; i < 4; i++) {
		data[i] = value >> (8 * i);
		data[i + 4] = ~data[i];
	}
}

inline Options options(bool bootPinEnabled = false) {
	Options data;
	data.fill(0xff);
	for (unsigned offset = 0; offset <= 48; offset += 8) {
		word(data.data() + offset, 0x12340000 + offset);
	}
	word(data.data(), bootPinEnabled ? 0xdeffe1aa : 0xdfffe1aa);
	word(data.data() + 112, 0);
	return data;
}

inline Options withBootPin(Options data) {
	data[3] &= ~1U;
	data[7] |= 1;
	return data;
}

struct Script : g0::BootTransport {
	std::vector<std::pair<uint8_t, uint8_t>> bytes;
	std::vector<size_t> reconnectAt;
	size_t offset = 0;
	uint32_t waited = 0;
	size_t reconnects = 0;
	uint8_t byte(uint8_t value) override {
		assert(offset < bytes.size());
		const auto expected = bytes[offset++];
		assert(value == expected.first);
		return expected.second;
	}
	void sleepMs(uint32_t delay) override {
		waited += delay;
	}
	void reconnect() override {
		assert(reconnects < reconnectAt.size() && reconnectAt[reconnects] == offset);
		reconnects++;
	}
	void send(std::initializer_list<uint8_t> values) {
		for (auto value : values) {
			bytes.emplace_back(value, 0x79); // Spurious MISO ACKs must not truncate a packet.
		}
	}
	void ack(uint8_t result = 0x79) {
		bytes.insert(bytes.end(), {{0, 0xa5}, {0, result}});
		if (result == 0x79) {
			bytes.emplace_back(0x79, 0xa5);
		}
	}
	void command(uint8_t value) {
		send({0x5a, value, uint8_t(value ^ 0xff)});
		ack();
	}
	void address(uint32_t value) {
		const uint8_t a = value >> 24, b = value >> 16, c = value >> 8, d = value;
		send({a, b, c, d, uint8_t(a ^ b ^ c ^ d)});
		ack();
	}
	void connect(uint16_t id = 0x460, uint8_t sync = 0x79) {
		send({0x5a});
		ack(sync);
		command(0x02);
		bytes.insert(bytes.end(), {{0, 0xa5}, {0, 1}, {0, uint8_t(id >> 8)}, {0, uint8_t(id)}});
		ack();
	}
	void read(uint32_t location, const uint8_t* data, size_t size) {
		command(0x11);
		address(location);
		send({uint8_t(size - 1), uint8_t((size - 1) ^ 0xff)});
		ack();
		bytes.emplace_back(0, 0xa5);
		for (size_t i = 0; i < size; i++) {
			bytes.emplace_back(0, data[i]);
		}
	}
	void write(uint32_t location, const uint8_t* data, size_t size, bool reloading = false, bool lostAck = false) {
		command(0x31);
		address(location);
		uint8_t checksum = size - 1;
		send({checksum});
		for (size_t i = 0; i < size; i++) {
			send({data[i]});
			checksum ^= data[i];
		}
		send({checksum});
		if (lostAck) {
			for (unsigned i = 0; i < 101; i++) {
				bytes.emplace_back(0, 0);
			}
		} else {
			ack();
		}
		if (reloading) {
			reconnectAt.push_back(bytes.size());
		}
	}
	void provision(const Options& original) {
		read(optionsAddress, original.data(), original.size());
		read(optionsAddress, original.data(), original.size());
		const auto changed = withBootPin(original);
		if (changed != original) {
			write(optionsAddress, changed.data(), changed.size(), true);
			connect();
			read(optionsAddress, changed.data(), changed.size());
		}
	}
	void flash(const uint8_t* data, size_t size, bool corrupt = false) {
		command(0x44);
		send({255, 255, 0});
		ack();
		for (size_t position = 0; position < size; position += 256) {
			std::array<uint8_t, 256> block;
			block.fill(0xff);
			for (size_t i = 0; i < block.size() && position + i < size; i++) {
				block[i] = data[position + i];
			}
			write(g0::Bootloader::FlashBase + position, block.data(), block.size());
			if (corrupt) {
				block[10] ^= 1;
			}
			read(g0::Bootloader::FlashBase + position, block.data(), block.size());
			if (corrupt) {
				break;
			}
		}
	}
	void go() {
		command(0x21);
		address(g0::Bootloader::FlashBase);
	}
};
} // namespace boot_test
