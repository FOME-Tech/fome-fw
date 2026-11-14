#include "bootloader_fixture.h"
#include <algorithm>

using namespace g0;
using namespace boot_test;

namespace {
std::vector<uint8_t> image() {
	std::vector<uint8_t> data(258, 0x79);
	const uint8_t vectors[] = {0, 0x90, 0, 0x20, 9, 0, 0, 8};
	std::copy(std::begin(vectors), std::end(vectors), data.begin());
	return data;
}

void badOptions(const Options& data) {
	Script io;
	io.connect();
	io.read(optionsAddress, data.data(), data.size());
	io.read(optionsAddress, data.data(), data.size());
	auto binary = image();
	assert(!Bootloader(io).flash(binary.data(), binary.size()));
	assert(io.offset == io.bytes.size()); // No option write or main-flash erase.
}
} // namespace

void testBootloader() {
	auto data = image();
	assert(Bootloader::validImage(data.data(), data.size()));
	assert(!Bootloader::validImage(nullptr, data.size()));
	assert(!Bootloader::validImage(data.data(), 7));
	assert(!Bootloader::validImage(data.data(), Bootloader::FlashSize + 1));
	for (bool alreadyEnabled : {false, true}) {
		Script io;
		io.connect();
		io.provision(options(alreadyEnabled));
		io.flash(data.data(), data.size());
		io.go();
		Bootloader loader(io);
		assert(loader.flash(data.data(), data.size()));
		assert(loader.go());
		assert(io.offset == io.bytes.size());
		assert(io.reconnects == (alreadyEnabled ? 0 : 1));
	}
	Script connected;
	connected.connect(0x460, 0x1f); // A ROM that is already synchronized can NACK sync.
	assert(Bootloader(connected).connect());
	assert(connected.offset == connected.bytes.size());
	Script wrongChip;
	wrongChip.connect(0x466);
	assert(!Bootloader(wrongChip).flash(data.data(), data.size()));
	assert(wrongChip.offset == wrongChip.bytes.size());

	auto invalid = options();
	invalid[4] ^= 1;
	badOptions(invalid); // Invalid complement.
	for (uint32_t user : {0xdfffe1ccU, 0xdfffe100U, 0xddffe1aaU, 0xd7ffe1aaU}) {
		auto config = options();
		word(config.data(), user);
		badOptions(config); // RDP levels, nBOOT1=0, NRST configured as GPIO.
	}
	invalid = options();
	word(invalid.data() + 112, 1U << 16);
	badOptions(invalid); // BOOT_LOCK must remain clear.

	Script unstable;
	const auto original = options();
	invalid = original;
	invalid[48] ^= 1;
	unstable.connect();
	unstable.read(optionsAddress, original.data(), original.size());
	unstable.read(optionsAddress, invalid.data(), invalid.size());
	assert(!Bootloader(unstable).flash(data.data(), data.size()));
	assert(unstable.offset == unstable.bytes.size());

	Script notWritten;
	notWritten.connect();
	notWritten.read(optionsAddress, original.data(), original.size());
	notWritten.read(optionsAddress, original.data(), original.size());
	const auto updated = withBootPin(original);
	notWritten.write(optionsAddress, updated.data(), updated.size(), true);
	notWritten.connect();
	notWritten.read(optionsAddress, original.data(), original.size()); // ROM rejected/failed option programming.
	assert(!Bootloader(notWritten).flash(data.data(), data.size()));
	assert(notWritten.offset == notWritten.bytes.size());

	Script lostAck;
	lostAck.connect();
	lostAck.read(optionsAddress, original.data(), original.size());
	lostAck.read(optionsAddress, original.data(), original.size());
	lostAck.write(optionsAddress, updated.data(), updated.size(), true, true);
	lostAck.connect();
	lostAck.read(optionsAddress, updated.data(), updated.size());
	lostAck.flash(data.data(), data.size());
	assert(Bootloader(lostAck).flash(data.data(), data.size()));
	assert(lostAck.offset == lostAck.bytes.size() && lostAck.reconnects == 1);

	Script corrupt;
	corrupt.connect();
	corrupt.provision(options(true));
	corrupt.flash(data.data(), data.size(), true);
	assert(!Bootloader(corrupt).flash(data.data(), data.size()));
	assert(corrupt.offset == corrupt.bytes.size());

	Script timeout;
	timeout.send({0x5a});
	for (unsigned i = 0; i < 101; i++) {
		timeout.bytes.emplace_back(0, 0);
	}
	timeout.send({0x5a, 0x02, 0xfd});
	for (unsigned i = 0; i < 101; i++) {
		timeout.bytes.emplace_back(0, 0);
	}
	assert(!Bootloader(timeout).flash(data.data(), data.size()));
	assert(timeout.waited == 200 && timeout.offset == timeout.bytes.size());
	data[4] = 8;
	Script untouched;
	assert(!Bootloader(untouched).flash(data.data(), data.size()));
	assert(untouched.offset == 0);
}
