#include "pch.h"
#include "service.h"
#include "spi_thread.h"
#include "mpu_util.h"

namespace g0 {
namespace {
// SPI DMA must not access stack or cached SRAM on H7.
NO_CACHE wire::AppFrame txBuffer;
NO_CACHE wire::AppFrame rxBuffer;
Link instance;

class Device final : public BackgroundSpiDevice {
public:
	SPIDriver* driver = nullptr;
	SPIConfig spiConfig = {};
	SPIDriver* spiDriver() const override {
		return driver;
	}
	const SPIConfig& config() override {
		return spiConfig;
	}
	int getSpiThreadPeriodMs() const override {
		return 10;
	}
	void performTransfer(SPIDriver& spi) override {
		instance.prepare(txBuffer, nowMs());
		rxBuffer = {};
		spiSelect(&spi);
		// This fome-fw ChibiOS HAL has a void synchronous spiExchange API.
		spiExchange(&spi, wire::appFrameSize, txBuffer.bytes, rxBuffer.bytes);
		spiUnselect(&spi);
		instance.complete(rxBuffer, true, nowMs());
	}
};
Device device;
bool started = false;
} // namespace

uint32_t nowMs() {
	return static_cast<uint32_t>(NT2US(getTimeNowNt()) / 1000);
}
Link& link() {
	return instance;
}

bool start(SPIDriver& driver, const SPIConfig& spiSettings) {
	if (started) {
		return true;
	}
	device.driver = &driver;
	device.spiConfig = spiSettings;
	started = registerBackgroundSpiDevice(device);
	return started;
}
} // namespace g0
