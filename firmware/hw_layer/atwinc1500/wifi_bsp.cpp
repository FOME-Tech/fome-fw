#include "pch.h"

#if EFI_WIFI

#include "ch.h"
#include "hal.h"

#include "digital_input_exti.h"

#include "bus_wrapper/include/nm_bus_wrapper.h"

// Implement these functions for your board for WiFi to work!
spi_device_e getWifiSpiDevice();
Gpio getWifiCsPin();
Gpio getWifiResetPin();
Gpio getWifiIsrPin();

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
		efiExtiEnablePin("WiFi ISR", getWifiIsrPin(), PAL_EVENT_MODE_FALLING_EDGE, isrAdapter, nullptr);
		isrEnabled = true;
	} else if (!u8Enable && isrEnabled) {
		efiExtiDisablePin(getWifiIsrPin());
		isrEnabled = false;
	}
}

void nm_bsp_register_isr(tpfNmBspIsr pfIsr) {
	gpfIsr = pfIsr;

	nm_bsp_interrupt_ctrl(1);
}

static SPIDriver* wifiSpi = nullptr;

tstrNmBusCapabilities egstrNmBusCapabilities = { .u16MaxTrxSz = 4096 };

#ifdef STM32H7XX
// H7 SPI clock is set to 80MHz
// fast mode is 80mhz/2 = 40MHz
static SPIConfig wifi_spicfg = {
		.circular = false,
		.end_cb = NULL,
		.ssport = NULL,
		.sspad = 0,
		.cfg1 = 7 // 8 bits per byte
			| 0 << 28 /* MBR = 0, divider = 2 */,
		.cfg2 = 0
};

#else // Not H7, ie F4/F7

// 168mhz F4: 42 or 21 MHz depending on which SPI device
// 216mhz F7: 54 or 27 MHz depending on whcih SPI device

static SPIConfig wifi_spicfg = {
		.circular = false,
		.end_cb = NULL,
		.ssport = NULL,
		.sspad = 0,
		.cr1 = SPI_BaudRatePrescaler_2,
		.cr2 = 0
};

#endif

static OutputPin wifiCs;
static OutputPin wifiReset;

sint8 nm_bus_init(void*) {
	auto spi = getWifiSpiDevice();
	if (spi == SPI_NONE) {
		return M2M_ERR_BUS_FAIL;
	}

	// Set up chip select, reset
	wifiCs.initPin("WiFi CS", getWifiCsPin());
	wifiCs.setValue(1);
	wifiReset.initPin("WiFi RST", getWifiResetPin());

	// Reset the chip
	wifiReset.setValue(0);
	chThdSleepMilliseconds(10);
	wifiReset.setValue(1);
	chThdSleepMilliseconds(10);

	// Set up SPI
	wifiSpi = getSpiDevice(spi);
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

static void resetSpiDevice(SPIDriver* spi) {
#if STM32_SPI_USE_SPI1
	if (spi == &SPID1) {
		rccResetSPI1();
	}
#endif // STM32_SPI_USE_SPI1

#if STM32_SPI_USE_SPI2
	if (spi == &SPID2) {
		rccResetSPI2();
	}
#endif // STM32_SPI_USE_SPI2

#if STM32_SPI_USE_SPI3
	if (spi == &SPID3) {
		rccResetSPI3();
	}
#endif // STM32_SPI_USE_SPI3

#if STM32_SPI_USE_SPI4
	if (spi == &SPID4) {
		rccResetSPI4();
	}
#endif // STM32_SPI_USE_SPI4

#if STM32_SPI_USE_SPI5
	if (spi == &SPID5) {
		rccResetSPI5();
	}
#endif // STM32_SPI_USE_SPI5

#if STM32_SPI_USE_SPI6
	if (spi == &SPID6) {
		rccResetSPI6();
	}
#endif // STM32_SPI_USE_SPI6
}

sint8 nm_spi_rw(uint8* pu8Mosi, uint8* pu8Miso, uint16 u16Sz) {
	if (u16Sz < 16) {
		spiSelectI(wifiSpi);

		for (size_t i = 0; i < u16Sz; i++) {
			uint8 tx = pu8Mosi ? pu8Mosi[i] : 0;

			uint8 rx = spiPolledExchange(wifiSpi, tx);

			if (pu8Miso) {
				pu8Miso[i] = rx;
			}
		}
	} else {
		// #if CORTEX_MODEL == 7
		// if (pu8Mosi) {
		// 	SCB_CleanDCache_by_Addr((uint32_t*)pu8Mosi, u16Sz);
		// }
		// #endif
		
		#ifdef STM32H7XX
		/* workaround for silicon errata */
		/* see https://github.com/rusefi/rusefi/issues/2395 */
		resetSpiDevice(wifiSpi);
		spiStart(wifiSpi, &wifi_spicfg);
		#endif

		spiSelectI(wifiSpi);
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

		// #if CORTEX_MODEL == 7
		// if (pu8Miso) {
		// 	SCB_InvalidateDCache_by_Addr((uint32_t*)pu8Miso, u16Sz);
		// }
		// #endif
	}

	spiUnselectI(wifiSpi);

	return M2M_SUCCESS;
}

#endif // EFI_WIFI
