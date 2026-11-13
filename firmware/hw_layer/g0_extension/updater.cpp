#include "pch.h"
#include "updater.h"
#include "bootloader.h"
#include "link.h"
#include "mpu_util.h"
#include "g0_firmware_image.h"

namespace g0 {
namespace {
static_assert(g0_image::version == AppVersion, "G0 image must match the protocol version");
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

private:
	SPIDriver& m_spi;
};
} // namespace

bool updateFirmware(SPIDriver& spi, const SPIConfig& spiSettings, brain_pin_e resetPin, brain_pin_e bootPin) {
	// Reject an invalid image before touching reset or entering the ROM loader.
	if (!Bootloader::validImage(g0_image::data, sizeof(g0_image::data))) {
		return false;
	}
	efiSetPadMode("G0 BOOT", bootPin, PAL_MODE_OUTPUT_PUSHPULL);
	efiSetPadMode("G0 RESET", resetPin, PAL_MODE_OUTPUT_PUSHPULL);
	spiAcquireBus(&spi);
	spiStart(&spi, &spiSettings);
	reset(resetPin, bootPin, false);
	bool ok = versionMatches(spi);
	if (!ok) {
		efiPrintf("G0: programming bundled firmware");
		reset(resetPin, bootPin, true);
		spiSelect(&spi);
		Transport transport(spi);
		Bootloader loader(transport);
		ok = loader.flash(g0_image::data, sizeof(g0_image::data));
		spiUnselect(&spi);
		reset(resetPin, bootPin, false);
		ok = ok && versionMatches(spi);
	}
	spiStop(&spi);
	spiReleaseBus(&spi);
	// Retain ownership: these are dedicated expansion control pins.
	pin(bootPin, false);
	pin(resetPin, true);
	efiPrintf("G0 firmware %s", ok ? "ready" : "failed");
	return ok;
}
} // namespace g0
