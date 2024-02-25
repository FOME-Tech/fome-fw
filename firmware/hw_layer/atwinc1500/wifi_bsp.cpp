#include "pch.h"

#include "ch.h"
#include "hal.h"

#include "digital_input_exti.h"

#include "bus_wrapper/include/nm_bus_wrapper.h"

void nm_bsp_sleep(uint32 u32TimeMsec) {
	chThdSleepMilliseconds(u32TimeMsec);
}

static tpfNmBspIsr gpfIsr = nullptr;

static void isrAdapter(void*, efitick_t) {
	if (gpfIsr) {
		gpfIsr();
	}
}

static bool isrEnabled = false;

void nm_bsp_interrupt_ctrl(uint8 u8Enable) {
	if (u8Enable && !isrEnabled) {
		efiExtiEnablePin("WiFi ISR", Gpio::G2, PAL_EVENT_MODE_FALLING_EDGE, isrAdapter, nullptr);
		isrEnabled = true;
	} else if (!u8Enable && isrEnabled) {
		efiExtiDisablePin(Gpio::G2);
		isrEnabled = false;
	}
}

void nm_bsp_register_isr(tpfNmBspIsr pfIsr) {
	gpfIsr = pfIsr;

	nm_bsp_interrupt_ctrl(1);
}

static SPIDriver* wifiSpi = nullptr;

#define NM_BUS_MAX_TRX_SZ	512

tstrNmBusCapabilities egstrNmBusCapabilities = { .u16MaxTrxSz = 512 };

static SPIConfig wifi_spicfg = {
		.circular = false,
		.end_cb = NULL,
		.ssport = NULL,
		.sspad = 0,
		.cr1 = SPI_BaudRatePrescaler_16, // SPI_BaudRatePrescaler_2,
		.cr2 = 0
};

static OutputPin wifiCs;
static OutputPin wifiReset;

sint8 nm_bus_init(void*) {
	auto pin = Gpio::D2;

	wifiCs.initPin("WiFi CS", pin);
	wifiCs.setValue(1);
	wifiReset.initPin("wifi rst", Gpio::G3);

	// Reset the chip
	wifiReset.setValue(0);
	chThdSleepMilliseconds(10);
	wifiReset.setValue(1);
	chThdSleepMilliseconds(10);

	wifiSpi = getSpiDevice(SPI_DEVICE_3);
	wifi_spicfg.ssport = wifiCs.m_port;
	wifi_spicfg.sspad = wifiCs.m_pin;

	spiStart(wifiSpi, &wifi_spicfg);

	// Take exclusive access of the bus for WiFi use, don't release it until the bus is de-init.
	spiAcquireBus(wifiSpi);

	return M2M_SUCCESS;
}

sint8 nm_bus_deinit(void) {
	spiReleaseBus(wifiSpi);
	spiStop(wifiSpi);

	return M2M_SUCCESS;
}

sint8 nm_bus_speed(uint8 /*level*/) {
	// Do we even need to do anything here?
	return M2M_SUCCESS;
}

sint8 nm_spi_rw(uint8* pu8Mosi, uint8* pu8Miso, uint16 u16Sz) {
	spiSelect(wifiSpi);

	if (u16Sz < 16) {
		for (size_t i = 0; i < u16Sz; i++) {
			uint8 tx = pu8Mosi ? pu8Mosi[i] : 0;

			uint8 rx = spiPolledExchange(wifiSpi, tx);

			if (pu8Miso) {
				pu8Miso[i] = rx;
			}
		}
	} else {
		if (pu8Mosi && pu8Miso) {
			spiExchange(wifiSpi, u16Sz, pu8Mosi, pu8Miso);
		} else if (pu8Mosi) {
			spiSend(wifiSpi, u16Sz, pu8Mosi);
		} else if (pu8Miso) {
			spiReceive(wifiSpi, u16Sz, pu8Miso);
		} else {
			// Neither MISO nor MOSI???
			osalSysHalt("wifi neither mosi nor miso");
		}
	}

	spiUnselect(wifiSpi);

	return M2M_SUCCESS;
}
