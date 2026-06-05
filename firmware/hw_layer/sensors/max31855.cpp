/**
 * @file max31855.cpp
 * @brief MAX31855 Thermocouple-to-Digital Converter driver
 *
 *
 * http://datasheets.maximintegrated.com/en/ds/MAX31855.pdf
 *
 *
 * Read-only communication over 5MHz SPI
 *
 * @date Sep 17, 2014
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"
#include "max31855.h"

#include "hardware.h"
#include "spi_thread.h"

#if EFI_PROD_CODE
#include "mpu_util.h"
#endif /* EFI_PROD_CODE */

#if EFI_MAX_31855

static SPIDriver* driver = nullptr;
static SPIConfig spiConfig;

// bits D17 and D3 are always expected to be zero
#define MC_RESERVED_BITS 0x20008
#define MC_OPEN_BIT 1
#define MC_GND_BIT 2
#define MC_VCC_BIT 4

#define GET_TEMPERATURE_C(x) (((x) >> 18) / 4)

class Max31855Channel final : public BackgroundSpiDevice, public StoredValueSensor {
public:
	Max31855Channel(SensorType type)
		: StoredValueSensor(type, MS2NT(1000)) {}

	void configure(Gpio pin) {
		m_csPort = getHwPort("spi", pin);
		m_csPin = getHwPin("spi", pin);

		// Unselect first so that we don't briefly blip the pin when configured as an output
		unselect();
		efiSetPadMode("MAX31855 CS", pin, PAL_STM32_MODE_OUTPUT);
	}

	bool isEnabled() const override {
		return true;
	}

	SPIDriver* spiDriver() const override {
		return driver;
	}

	const SPIConfig& config() override {
		return spiConfig;
	}

	int getSpiThreadPeriodMs() const override {
		return 100;
	}

	void performTransfer(SPIDriver& spi) override {
		union {
			uint32_t egtPacket;
			uint8_t egtBytes[4];
		};

		select();

		for (int i = sizeof(egtBytes) - 1; i >= 0; i--) {
			egtBytes[i] = spiPolledExchange(&spi, 0);
		}

		unselect();

		if (egtPacket & MC_RESERVED_BITS) {
			invalidate(UnexpectedCode::Configuration);
		} else if (egtPacket & MC_OPEN_BIT) {
			invalidate(UnexpectedCode::Inconsistent);
		} else if (egtPacket & MC_GND_BIT) {
			invalidate(UnexpectedCode::Low);
		} else if (egtPacket & MC_VCC_BIT) {
			invalidate(UnexpectedCode::High);
		} else {
			setValidValue(GET_TEMPERATURE_C(egtPacket), getTimeNowNt());
		}
	}

private:
	ioportid_t m_csPort;
	ioportmask_t m_csPin;

	void select() {
		palClearPad(m_csPort, m_csPin);
	}

	void unselect() {
		palSetPad(m_csPort, m_csPin);
	}
};

static Max31855Channel max31855Channels[EGT_CHANNEL_COUNT] = {
		Max31855Channel(SensorType::EGT1),
		Max31855Channel(SensorType::EGT2),
		Max31855Channel(SensorType::EGT3),
		Max31855Channel(SensorType::EGT4),
		Max31855Channel(SensorType::EGT5),
		Max31855Channel(SensorType::EGT6),
		Max31855Channel(SensorType::EGT7),
		Max31855Channel(SensorType::EGT8),
};

void initMax31855() {
	if (!engineConfiguration->max31855enable) {
		driver = nullptr;
		return;
	}

	auto device = engineConfiguration->max31855spiDevice;
	driver = getSpiDevice(device);
	if (!driver) {
		return;
	}

	spiConfig.end_cb = nullptr;
	spiConfig.ssport = nullptr;
	spiConfig.sspad = 0;
#ifdef STM32H7XX
	spiConfig.cfg1 = 7 // 8 bits per byte
				   | SPI_CFG1_MBR_DIV16;
#else
	spiConfig.cr1 = getSpiPrescaler(_5MHz, device);
#endif

	for (int i = 0; i < EGT_CHANNEL_COUNT; i++) {
		auto pin = engineConfiguration->max31855_cs[i];
		if (isBrainPinValid(pin)) {
			max31855Channels[i].configure(pin);
			registerBackgroundSpiDevice(max31855Channels[i]);
			max31855Channels[i].Register();
		}
	}
}

#endif /* EFI_MAX_31855 */
