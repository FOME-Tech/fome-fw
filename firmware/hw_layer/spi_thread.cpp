#include "pch.h"

#include "spi_thread.h"

#if HAL_USE_SPI

#include "periodic_thread_controller.h"

namespace {
constexpr size_t MaxBackgroundSpiDevices = 16;
constexpr int SpiThreadRateHz = 100;

BackgroundSpiDevice* devices[MaxBackgroundSpiDevices] = {};
int deviceCount = 0;
efitick_t lastPollTimes[MaxBackgroundSpiDevices] = {};

class BackgroundSpiController final : public PeriodicController<UTILITY_THREAD_STACK_SIZE> {
public:
	BackgroundSpiController()
		: PeriodicController("SpiWorker", PRIO_AUX_SPI, SpiThreadRateHz) {}

private:
	void PeriodicTask(efitick_t nowNt) override {
		BackgroundSpiDevice* localDevices[MaxBackgroundSpiDevices];
		int count;

		chSysLock();
		count = deviceCount;
		for (int i = 0; i < count; i++) {
			localDevices[i] = devices[i];
		}
		chSysUnlock();

		for (int i = 0; i < count; i++) {
			auto* device = localDevices[i];
			if (!device || !device->isEnabled()) {
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
	chSysLock();
	const int count = deviceCount;

	for (int i = 0; i < count; i++) {
		if (devices[i] == &device) {
			chSysUnlock();
			backgroundSpiController.startThread();
			return true;
		}
	}

	if (count >= static_cast<int>(MaxBackgroundSpiDevices)) {
		chSysUnlock();
		firmwareError(ObdCode::CUSTOM_ERR_UNEXPECTED_SPI, "Too many background SPI devices");
		return false;
	}

	devices[count] = &device;
	deviceCount = count + 1;
	chSysUnlock();
	backgroundSpiController.startThread();
	return true;
}

#endif
