#include "pch.h"
#include "updater.h"
#include "bootloader.h"
#include "g0_firmware_image.h"
#include "link.h"
#include "mpu_util.h"

namespace g0 {
namespace {
NO_CACHE wire::AppFrame tx;
NO_CACHE wire::AppFrame rx;

void pin(brain_pin_e line, bool high) {
	palWritePad(getHwPort("G0", line), getHwPin("G0", line), high);
}

void reset(brain_pin_e resetPin, brain_pin_e bootPin, bool rom) {
	pin(bootPin, rom);
	chThdSleepMilliseconds(5);
	pin(resetPin, false);
	chThdSleepMilliseconds(20);
	pin(resetPin, true);
	chThdSleepMilliseconds(100);
}

void appExchange(SPIDriver& spi, uint8_t command) {
	tx = {};
	rx = {};
	tx.bytes[0] = command;
	wire::seal(tx);
	spiSelect(&spi);
	spiExchange(&spi, wire::appFrameSize, tx.bytes, rx.bytes);
	spiUnselect(&spi);
	chThdSleepMilliseconds(2);
}

bool versionMatches(SPIDriver& spi) {
	appExchange(spi, wire::cmdReadVersion);
	appExchange(spi, wire::cmdNop);
	wire::AppFrame request = {};
	request.bytes[0] = wire::cmdReadVersion;
	return validResponse(request, rx) && rx.versionResponse.version == g0_image::version;
}

class Transport final : public BootTransport {
public:
	explicit Transport(SPIDriver& spi)
		: m_spi(spi) {}
	uint8_t byte(uint8_t value) override {
		tx.bytes[0] = value;
		spiExchange(&m_spi, 1, tx.bytes, rx.bytes);
		chThdSleepMicroseconds(20);
		return rx.bytes[0];
	}
	void sleepMs(uint32_t delay) override {
		chThdSleepMilliseconds(delay);
	}
	void reconnect() override {
		spiUnselect(&m_spi);
		chThdSleepMilliseconds(100);
		spiSelect(&m_spi);
	}

private:
	SPIDriver& m_spi;
};
} // namespace

bool updateFirmware(SPIDriver& spi, const SPIConfig& spiSettings, brain_pin_e resetPin, brain_pin_e bootPin) {
	// Reject an invalid image before touching reset or entering the ROM loader.
	if (!Bootloader::validImage(g0_image::data, sizeof(g0_image::data))) {
		return false;
	}
	// Preserve the factory ROM session: NRST must never briefly go low during GPIO setup.
	pin(resetPin, true);
	pin(bootPin, true);
	efiSetPadMode("G0 BOOT", bootPin, PAL_MODE_OUTPUT_PUSHPULL);
	efiSetPadMode("G0 RESET", resetPin, PAL_MODE_OUTPUT_PUSHPULL);
	spiAcquireBus(&spi);
	spiStart(&spi, &spiSettings);
	chThdSleepMilliseconds(100);
	Transport transport(spi);
	Bootloader loader(transport);
	spiSelect(&spi);
	bool inRom = loader.connect();
	bool ok = false;
	if (!inRom) {
		spiUnselect(&spi);
		reset(resetPin, bootPin, false);
		ok = versionMatches(spi);
		if (!ok) {
			reset(resetPin, bootPin, true);
			spiSelect(&spi);
			inRom = loader.connect();
		}
	}
	if (inRom) {
		efiPrintf("G0: provisioning and programming bundled firmware");
		ok = loader.flash(g0_image::data, sizeof(g0_image::data));
		pin(bootPin, false);
		// Go bypasses the latched EMPTY flag on first programming, including older ROMs.
		ok = ok && loader.go();
		spiUnselect(&spi);
		chThdSleepMilliseconds(100);
		ok = ok && versionMatches(spi);
	}
	spiUnselect(&spi);
	spiStop(&spi);
	spiReleaseBus(&spi);
	// Retain ownership: these are dedicated expansion control pins.
	pin(bootPin, false);
	pin(resetPin, true);
	efiPrintf("G0 firmware %s", ok ? "ready" : "failed");
	return ok;
}
} // namespace g0
