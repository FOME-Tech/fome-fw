#include "pch.h"

#include "spi_thread.h"

#if HAL_USE_SPI

#include "periodic_thread_controller.h"

namespace {
constexpr size_t MaxBackgroundSpiDevices = 16;
constexpr int SpiThreadRateHz = 100;

BackgroundSpiDevice* devices[MaxBackgroundSpiDevices] = {};
size_t deviceCount = 0;
efitick_t lastPollTimes[MaxBackgroundSpiDevices] = {};

class BackgroundSpiController final : public PeriodicController<UTILITY_THREAD_STACK_SIZE> {
public:
	BackgroundSpiController()
		: PeriodicController("SpiWorker", PRIO_AUX_SPI, SpiThreadRateHz) {}

private:
	void PeriodicTask(efitick_t nowNt) override {
		for (size_t i = 0; i < deviceCount; i++) {
			auto* device = devices[i];
			if (!device) {
				continue;
			}

			const int periodMs = maxI(1, device->getSpiThreadPeriodMs());
			const auto periodNt = MS2NT(periodMs);
			const auto lastPoll = lastPollTimes[i];

			if (lastPoll != 0 && (nowNt - lastPoll) < periodNt) {
				continue;
			}

			auto* driver = device->spiDriver();
			if (!driver) {
				continue;
			}

			spiAcquireBus(driver);
			spiStart(driver, &device->config());
			device->performTransfer(*driver);
			spiStop(driver);
			spiReleaseBus(driver);

			lastPollTimes[i] = nowNt;
		}
	}
};

BackgroundSpiController backgroundSpiController;
} // namespace

bool registerBackgroundSpiDevice(BackgroundSpiDevice& device) {
	// Avoid double registrations
	for (size_t i = 0; i < deviceCount; i++) {
		if (devices[i] == &device) {
			return true;
		}
	}

	// Avoid too many registrations
	if (deviceCount >= MaxBackgroundSpiDevices) {
		firmwareError(ObdCode::CUSTOM_ERR_UNEXPECTED_SPI, "Too many background SPI devices");
		return false;
	}

	devices[deviceCount] = &device;
	deviceCount++;
	backgroundSpiController.startThread();
	return true;
}

#endif
