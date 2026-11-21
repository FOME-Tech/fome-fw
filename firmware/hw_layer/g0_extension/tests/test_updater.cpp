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
		assert(testPinOutputs[Gpio::B15]); // Drive BOOT0 throughout reset selection.
		testResetPulses.push_back(Gpio::B14);
		if (romBytes && !missingDevice && !appRunning) {
			assert(rom->offset == rom->bytes.size()); // Never reset a factory ROM before Go.
		}
	}
	if (pin == Gpio::B14 && !testPins[pin] && high) {
		appRunning = hasApplication && !testPins[Gpio::B15];
	}
	testPins[pin] = high;
}

void spiExchange(SPIDriver*, size_t size, const void* tx, void* rx) {
	assert(testSelected && testStarted && testBusLocked);
	assert(testPinOutputs[Gpio::B15]); // Release SWCLK only after initialization finishes.
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
	testWaited = testPolledUs = testShortSleeps = 0;
	enginePins.runningLedPin.toggles = 0;
	assert(!testSelected && !testStarted && !testBusLocked);
}

static void finished() {
	assert(!testSelected && !testStarted && !testBusLocked);
	assert(!testPins[Gpio::B15] && testPins[Gpio::B14]);
	assert(!testPinOutputs[Gpio::B15] && testPinOutputs[Gpio::B14]);
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
	assert(testResetPulses.size() == 1); // Reset after successful Go, never before factory provisioning.
	assert(testPolledUs == romBytes * 20);
	assert(testShortSleeps == romBytes / 32);
	assert(testWaited < 1500); // No scheduler-tick delay per byte.
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
	assert(older.offset == older.bytes.size() && testResetPulses.size() == 3);
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

	boot_test::Script failedGo;
	failedGo.connect();
	failedGo.provision(boot_test::options(true));
	failedGo.flash(g0_image::data, sizeof(g0_image::data));
	failedGo.go();
	failedGo.bytes.pop_back(); // Go address phase NACK: never reset into
							   // unconfirmed startup.
	failedGo.bytes.back().second = 0x1f;
	init(failedGo, false);
	romStartsApp = false;
	assert(!g0::updateFirmware(spi, config, Gpio::B14, Gpio::B15));
	assert(failedGo.offset == failedGo.bytes.size() && testResetPulses.empty());
	finished();

	g0::Transport transport(spi);
	transport.sleepMs(250);
	assert(enginePins.runningLedPin.toggles > 0);

	boot_test::Script absent;
	init(absent, false);
	missingDevice = true;
	assert(!g0::updateFirmware(spi, config, Gpio::B14, Gpio::B15));
	assert(absent.offset == 0 && testResetPulses.size() == 2);
	finished();
}
