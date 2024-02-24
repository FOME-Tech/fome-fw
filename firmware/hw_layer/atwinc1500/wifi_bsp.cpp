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

void nm_bsp_interrupt_ctrl(uint8 u8Enable) {
	if (u8Enable) {
		efiExtiEnablePin("WiFi ISR", Gpio::A0, PAL_EVENT_MODE_FALLING_EDGE, isrAdapter, nullptr);
	} else {
		efiExtiDisablePin(Gpio::A0);
	}
}

void nm_bsp_register_isr(tpfNmBspIsr pfIsr) {
	gpfIsr = pfIsr;

	nm_bsp_interrupt_ctrl(1);
}

static SPIDriver* wifiSpi = &SPID1;

#define NM_BUS_MAX_TRX_SZ	512

tstrNmBusCapabilities egstrNmBusCapabilities = { .u16MaxTrxSz = 512 };

static SPIConfig wifi_spicfg = {
		.circular = false,
		.end_cb = NULL,
		.ssport = NULL,
		.sspad = 0,
		.cr1 = SPI_BaudRatePrescaler_2,
		.cr2 = 0
};

sint8 nm_bus_init(void*) {
	spiStart(wifiSpi, &wifi_spicfg);

	return M2M_SUCCESS;
}

sint8 nm_bus_deinit(void) {
	spiStop(wifiSpi);

	return M2M_SUCCESS;
}

sint8 nm_bus_speed(uint8 level) {
	// Do we even need to do anything here?
	return M2M_SUCCESS;
}

sint8 nm_spi_rw(uint8* pu8Mosi, uint8* pu8Miso, uint16 u16Sz) {
	spiAcquireBus(wifiSpi);

	if (u16Sz < 16) {
		for (size_t i = 0; i < u16Sz; i++) {
			pu8Miso[i] = spiPolledExchange(wifiSpi, pu8Mosi[i]);
		}
	} else {
		spiExchange(wifiSpi, u16Sz, pu8Mosi, pu8Miso);
	}

	spiReleaseBus(wifiSpi);

	return M2M_SUCCESS;
}
