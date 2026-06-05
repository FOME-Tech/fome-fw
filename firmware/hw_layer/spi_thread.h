/*
 * @file spi_thread.h
 *
 * Shared low-priority SPI worker for peripherals that should never force
 * timing-sensitive threads to perform synchronous SPI transactions.
 */

#pragma once

#if HAL_USE_SPI

class BackgroundSpiDevice {
public:
	virtual SPIDriver* spiDriver() const = 0;
	virtual const SPIConfig& config() = 0;
	virtual int getSpiThreadPeriodMs() const = 0;
	virtual void performTransfer(SPIDriver& driver) = 0;
};

bool registerBackgroundSpiDevice(BackgroundSpiDevice& device);

#endif // HAL_USE_SPI
