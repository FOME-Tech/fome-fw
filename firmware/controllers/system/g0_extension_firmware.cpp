#include "g0_extension_firmware.h"
#include "g0_extension_firmware_impl.h"

#if HW_ATLAS && HAL_USE_SPI

#include "hardware.h"
#include "mpu_util.h"

namespace g0_extension_firmware {
namespace {

static SPIConfig g0ExtensionSpiConfig = {
		.circular = false,
		.end_cb = NULL,
		.ssport = NULL,
		.sspad = 0,
		.cfg1 = 7 | SPI_CFG1_MBR_2 | SPI_CFG1_MBR_1 | SPI_CFG1_MBR_0,
		.cfg2 = 0};

static NO_CACHE uint8_t g0ExtensionSpiTxBuffer[SpiDmaBufferSize];
static NO_CACHE uint8_t g0ExtensionSpiRxBuffer[SpiDmaBufferSize];

} // namespace

SPIConfig& spiConfig() {
	return g0ExtensionSpiConfig;
}

uint8_t* txBuffer() {
	return g0ExtensionSpiTxBuffer;
}

uint8_t* rxBuffer() {
	return g0ExtensionSpiRxBuffer;
}

void setControlPin(brain_pin_e pin, bool value) {
	palWritePad(getHwPort("g0", pin), getHwPin("g0", pin), value);
}

void initControlPins() {
	efiSetPadMode("G0 BOOT", BootPin, PAL_MODE_OUTPUT_PUSHPULL);
	efiSetPadMode("G0 RESET", ResetPin, PAL_MODE_OUTPUT_PUSHPULL);

	setControlPin(BootPin, false);
	setControlPin(ResetPin, true);
}

void releaseControlPins() {
	setControlPin(BootPin, false);
	setControlPin(ResetPin, true);

	efiSetPadModeWithoutOwnershipAcquisition("G0 BOOT", BootPin, PAL_MODE_INPUT);
	efiSetPadModeWithoutOwnershipAcquisition("G0 RESET", ResetPin, PAL_MODE_INPUT);
}

void resetExtension(bool bootloaderMode) {
	setControlPin(BootPin, bootloaderMode);
	chThdSleepMilliseconds(5);

	setControlPin(ResetPin, false);
	chThdSleepMilliseconds(20);

	setControlPin(ResetPin, true);
	chThdSleepMilliseconds(100);
}

} // namespace g0_extension_firmware

#endif

bool loadG0ExtensionFirmware(bool forceUpdate) {
#if HW_ATLAS && HAL_USE_SPI
	#if G0_EXTENSION_FIRMWARE_IMAGE_AVAILABLE
	g0_extension_firmware::initControlPins();

	turnOnSpi(g0_extension_firmware::SpiDevice);

	SPIDriver* spi = getSpiDevice(g0_extension_firmware::SpiDevice);
	spiAcquireBus(spi);

	auto& spiCfg = g0_extension_firmware::spiConfig();
	initSpiCs(&spiCfg, g0_extension_firmware::SpiCsPin);
	palSetPad(spiCfg.ssport, spiCfg.sspad);
	spiStart(spi, &spiCfg);
	g0_extension_firmware::resetExtension(false);

	uint32_t currentVersion = 0;
	bool ok = true;

	if (g0_extension_firmware::readAppVersion(spi, currentVersion)) {
		efiPrintf(
				"G0 extension firmware: app version %d, bundled version %d",
				static_cast<int>(currentVersion),
				static_cast<int>(build_g0_extension_version));

		if (!forceUpdate && currentVersion == build_g0_extension_version) {
			efiPrintf("G0 extension firmware: firmware is already current");
		} else {
			efiPrintf("G0 extension firmware: requesting app update mode%s", forceUpdate ? " (forced)" : "");
			g0_extension_firmware::requestAppUpdate(spi);
			ok = g0_extension_firmware::flashFirmwareImageWithRetry(spi);
		}
	} else {
		efiPrintf("G0 extension firmware: no app version response, forcing update");
		ok = g0_extension_firmware::flashFirmwareImageWithRetry(spi);
	}

	spiReleaseBus(spi);
	g0_extension_firmware::releaseControlPins();

	if (ok) {
		efiPrintf("G0 extension firmware: complete");
	} else {
		efiPrintf("G0 extension firmware: failed");
	}

	return ok;
	#else
	efiPrintf("G0 extension firmware ERROR: run `make -C firmware/ext/g0_firmware for_fome_image` first");
	return false;
	#endif
#else
	(void)forceUpdate;
	efiPrintf("G0 extension firmware is only available on Atlas with SPI enabled");
	return false;
#endif
}
