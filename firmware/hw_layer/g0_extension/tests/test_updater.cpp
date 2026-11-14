#include "../updater.cpp"
#include "bootloader_fixture.h"

namespace {
boot_test::Script* rom;
bool appRunning;
bool hasApplication;
bool romStartsApp;
bool missingDevice;
uint32_t appVersion;
unsigned appExchanges;
unsigned romBytes;
} // namespace

void palWritePad(unsigned, unsigned pin, bool high) {
	if (pin == Gpio::B14 && testPinOutputs[pin] && !high) {
		testResetPulses.push_back(Gpio::B14);
	}
	if (pin == Gpio::B14 && !testPins[pin] && high) {
		appRunning = hasApplication && !testPins[Gpio::B15];
	}
	testPins[pin] = high;
}

void spiExchange(SPIDriver*, size_t size, const void* tx, void* rx) {
	assert(testSelected && testStarted && testBusLocked);
	if (size == g0::wire::appFrameSize) {
		appExchanges++;
		auto& reply = *static_cast<g0::wire::AppFrame*>(rx);
		if (appRunning) {
			reply = {};
			reply.versionResponse = {
					{0, 0, g0::wire::cmdReadVersion, g0::wire::versionPayloadLength},
					appVersion,
					g0::wire::protocolVersion};
			g0::wire::seal(reply);
		}
		return;
	}
	assert(size == 1);
	romBytes++;
	if (appRunning || missingDevice) {
		*static_cast<uint8_t*>(rx) = 0;
		return;
	}
	*static_cast<uint8_t*>(rx) = rom->byte(*static_cast<const uint8_t*>(tx));
	if (romStartsApp && rom->offset == rom->bytes.size()) {
		assert(!testPins[Gpio::B15]); // BOOT0 is LOW before Go starts the application.
		appRunning = true;
		hasApplication = true;
		appVersion = g0_image::version;
	}
}

static void init(boot_test::Script& script, bool running) {
	rom = &script;
	appRunning = running;
	hasApplication = running;
	romStartsApp = true;
	missingDevice = false;
	appVersion = g0_image::version;
	appExchanges = romBytes = 0;
	testPins[Gpio::B14] = true;
	testPins[Gpio::B15] = false;
	testPinOutputs[Gpio::B14] = testPinOutputs[Gpio::B15] = false;
	testResetPulses.clear();
	assert(!testSelected && !testStarted && !testBusLocked);
}

static void finished() {
	assert(!testSelected && !testStarted && !testBusLocked);
	assert(!testPins[Gpio::B15] && testPins[Gpio::B14]);
}

int main() {
	SPIDriver spi;
	SPIConfig config;
	boot_test::Script factory;
	factory.connect();
	factory.provision(boot_test::options());
	factory.flash(g0_image::data, sizeof(g0_image::data));
	factory.go();
	init(factory, false);
	assert(g0::updateFirmware(spi, config, Gpio::B14, Gpio::B15));
	assert(testResetPulses.empty()); // No NRST pulse before provisioning, or after first programming.
	assert(factory.offset == factory.bytes.size() && appExchanges == 2);
	finished();

	boot_test::Script upToDate;
	init(upToDate, true);
	assert(g0::updateFirmware(spi, config, Gpio::B14, Gpio::B15));
	assert(upToDate.offset == 0 && testResetPulses.size() == 1);
	finished();

	boot_test::Script older;
	older.connect();
	older.provision(boot_test::options(true));
	older.flash(g0_image::data, sizeof(g0_image::data));
	older.go();
	init(older, true);
	appVersion--;
	assert(g0::updateFirmware(spi, config, Gpio::B14, Gpio::B15));
	assert(older.offset == older.bytes.size() && testResetPulses.size() == 2);
	finished();

	boot_test::Script invalidOptions;
	invalidOptions.connect();
	auto invalid = boot_test::options();
	invalid[4] ^= 1;
	invalidOptions.read(boot_test::optionsAddress, invalid.data(), invalid.size());
	invalidOptions.read(boot_test::optionsAddress, invalid.data(), invalid.size());
	init(invalidOptions, false);
	romStartsApp = false;
	assert(!g0::updateFirmware(spi, config, Gpio::B14, Gpio::B15));
	assert(invalidOptions.offset == invalidOptions.bytes.size() && testResetPulses.empty());
	assert(!hasApplication);
	finished();

	boot_test::Script absent;
	init(absent, false);
	missingDevice = true;
	assert(!g0::updateFirmware(spi, config, Gpio::B14, Gpio::B15));
	assert(absent.offset == 0 && testResetPulses.size() == 2);
	finished();
}
