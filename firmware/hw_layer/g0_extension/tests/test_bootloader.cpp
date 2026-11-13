#include "../bootloader.h"
#include <cassert>
#include <utility>
#include <vector>

using namespace g0;

namespace {
struct Script : BootTransport {
	std::vector<std::pair<uint8_t, uint8_t>> bytes;
	size_t offset = 0;
	uint32_t waited = 0;
	uint8_t byte(uint8_t value) override {
		assert(offset < bytes.size());
		const auto expected = bytes[offset++];
		assert(value == expected.first);
		return expected.second;
	}
	void sleepMs(uint32_t delay) override {
		waited += delay;
	}
	void send(std::initializer_list<uint8_t> values) {
		// Spurious MISO ACKs while sending must not truncate the outgoing packet.
		for (auto value : values) {
			bytes.emplace_back(value, 0x79);
		}
	}
	void ack() {
		bytes.insert(bytes.end(), {{0, 0xa5}, {0, 0x79}, {0x79, 0xa5}});
	}
};

std::vector<uint8_t> image() {
	std::vector<uint8_t> data(258, 0x79);
	// SP = top of 36 KiB RAM, reset vector = flash + 9 (Thumb).
	const uint8_t vectors[] = {0, 0x90, 0, 0x20, 9, 0, 0, 8};
	for (size_t i = 0; i < 8; i++) {
		data[i] = vectors[i];
	}
	return data;
}

Script flashScript(const std::vector<uint8_t>& data, bool corrupt) {
	Script io;
	io.send({0x5a});
	io.ack();
	io.send({0x5a, 0x44, 0xbb});
	io.ack();
	io.send({255, 255, 0});
	io.ack();
	for (unsigned block = 0; block < 2; block++) {
		const uint8_t a[] = {8, 0, uint8_t(block), 0, uint8_t(8 ^ block)};
		io.send({0x5a, 0x31, 0xce});
		io.ack();
		io.send({a[0], a[1], a[2], a[3], a[4]});
		io.ack();
		io.send({255});
		uint8_t checksum = 255;
		for (size_t i = 0; i < 256; i++) {
			const size_t offset = block * 256 + i;
			const uint8_t value = offset < data.size() ? data[offset] : 255;
			io.bytes.emplace_back(value, 0x79);
			checksum ^= value;
		}
		io.send({checksum});
		io.ack();
		io.send({0x5a, 0x11, 0xee});
		io.ack();
		io.send({a[0], a[1], a[2], a[3], a[4]});
		io.ack();
		io.send({255, 0});
		io.ack();
		io.bytes.emplace_back(0, 0xa5);
		for (size_t i = 0; i < 256; i++) {
			const size_t offset = block * 256 + i;
			uint8_t value = offset < data.size() ? data[offset] : 255;
			if (corrupt && block == 0 && i == 10) {
				value ^= 1;
			}
			io.bytes.emplace_back(0, value);
		}
		if (corrupt) {
			break;
		}
	}
	return io;
}
} // namespace

void testBootloader() {
	auto data = image();
	assert(Bootloader::validImage(data.data(), data.size()));
	assert(!Bootloader::validImage(nullptr, data.size()));
	assert(!Bootloader::validImage(data.data(), 7));
	assert(!Bootloader::validImage(data.data(), Bootloader::FlashSize + 1));
	auto io = flashScript(data, false);
	Bootloader loader(io);
	assert(loader.flash(data.data(), data.size()));
	assert(io.offset == io.bytes.size());
	auto corrupt = flashScript(data, true);
	assert(!Bootloader(corrupt).flash(data.data(), data.size()));
	assert(corrupt.offset == corrupt.bytes.size());
	Script nack;
	nack.send({0x5a});
	nack.bytes.insert(nack.bytes.end(), {{0, 0xa5}, {0, 0x1f}});
	assert(!Bootloader(nack).flash(data.data(), data.size()));
	Script timeout;
	timeout.send({0x5a});
	for (unsigned i = 0; i < 1001; i++) {
		timeout.bytes.emplace_back(0, 0);
	}
	assert(!Bootloader(timeout).flash(data.data(), data.size()));
	assert(timeout.waited == 1000);
	data[4] = 8; // ARM vector instead of Thumb.
	Script untouched;
	assert(!Bootloader(untouched).flash(data.data(), data.size()));
	assert(untouched.offset == 0);
}
