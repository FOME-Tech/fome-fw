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

#define EGT_ERROR_VALUE -1000

static SPIDriver* driver = nullptr;
static SPIConfig spiConfig;
static bool isChannelConfigured[EGT_CHANNEL_COUNT];
static ioportid_t csPorts[EGT_CHANNEL_COUNT];
static ioportmask_t csPads[EGT_CHANNEL_COUNT];
static std::atomic<uint16_t> cachedEgtValues[EGT_CHANNEL_COUNT];

static void showEgtInfo() {
#if EFI_PROD_CODE
	efiPrintf("EGT spi: %d", engineConfiguration->max31855spiDevice);

	for (int i = 0; i < EGT_CHANNEL_COUNT; i++) {
		if (isBrainPinValid(engineConfiguration->max31855_cs[i])) {
			efiPrintf("%d ETG @ %s", i, hwPortname(engineConfiguration->max31855_cs[i]));
		}
	}
#endif
}

// bits D17 and D3 are always expected to be zero
#define MC_RESERVED_BITS 0x20008
#define MC_OPEN_BIT 1
#define MC_GND_BIT 2
#define MC_VCC_BIT 4

typedef enum {
	MC_OK = 0,
	MC_INVALID = 1,
	MC_OPEN = 2,
	MC_SHORT_GND = 3,
	MC_SHORT_VCC = 4,
} max_32855_code;

static const char* getMcCode(max_32855_code code) {
	switch (code) {
		case MC_OK:
			return "Ok";
		case MC_OPEN:
			return "Open";
		case MC_SHORT_GND:
			return "short gnd";
		case MC_SHORT_VCC:
			return "short VCC";
		default:
			return "invalid";
	}
}

static max_32855_code getResultCode(uint32_t egtPacket) {
	if ((egtPacket & MC_RESERVED_BITS) != 0) {
		return MC_INVALID;
	} else if ((egtPacket & MC_OPEN_BIT) != 0) {
		return MC_OPEN;
	} else if ((egtPacket & MC_GND_BIT) != 0) {
		return MC_SHORT_GND;
	} else if ((egtPacket & MC_VCC_BIT) != 0) {
		return MC_SHORT_VCC;
	} else {
		return MC_OK;
	}
}

static void selectChannel(int channel) {
	palClearPad(csPorts[channel], csPads[channel]);
}

static void unselectChannel(int channel) {
	palSetPad(csPorts[channel], csPads[channel]);
}

static void initChipSelect(int channel, brain_pin_e csPin) {
	csPorts[channel] = getHwPort("spi", csPin);
	csPads[channel] = getHwPin("spi", csPin);
	efiSetPadMode("chip select", csPin, PAL_STM32_MODE_OUTPUT);
	unselectChannel(channel);
}

static uint32_t readEgtPacket(SPIDriver& spi, int channel) {
	union {
		uint32_t egtPacket;
		uint8_t egtBytes[4];
	};

	selectChannel(channel);

	for (int i = sizeof(egtBytes) - 1; i >= 0; i--) {
		egtBytes[i] = spiPolledExchange(&spi, 0);
	}

	unselectChannel(channel);
	return egtPacket;
}

#define GET_TEMPERATURE_C(x) (((x) >> 18) / 4)

static uint16_t decodeEgtValue(uint32_t packet) {
	max_32855_code code = getResultCode(packet);
	if (code != MC_OK) {
		return EGT_ERROR_VALUE + code;
	} else {
		return GET_TEMPERATURE_C(packet);
	}
}

static bool tryGetEgtErrorCode(uint16_t rawValue, max_32855_code& code) {
	const auto signedValue = static_cast<int16_t>(rawValue);
	if (signedValue > EGT_ERROR_VALUE + MC_SHORT_VCC) {
		return false;
	}

	code = static_cast<max_32855_code>(signedValue - EGT_ERROR_VALUE);
	return true;
}

class Max31855Channel final : public BackgroundSpiDevice {
public:
	explicit Max31855Channel(int channel)
		: m_channel(channel) {}

	bool isSpiThreadEnabled() const override {
		return driver && isChannelConfigured[m_channel];
	}

	SPIDriver* getSpiThreadDriver() const override {
		return driver;
	}

	SPIConfig* getSpiThreadConfig() override {
		return &spiConfig;
	}

	int getSpiThreadPeriodMs() const override {
		return 100;
	}

	void performSpiTransfer(SPIDriver& spi) override {
		cachedEgtValues[m_channel].store(decodeEgtValue(readEgtPacket(spi, m_channel)), std::memory_order_relaxed);
	}

private:
	const int m_channel;
};

static Max31855Channel max31855Channels[] = {
		Max31855Channel(0),
		Max31855Channel(1),
		Max31855Channel(2),
		Max31855Channel(3),
		Max31855Channel(4),
		Max31855Channel(5),
		Max31855Channel(6),
		Max31855Channel(7),
};

uint16_t getMax31855EgtValue(int egtChannel) {
	if (egtChannel < 0 || egtChannel >= EGT_CHANNEL_COUNT) {
		return EGT_ERROR_VALUE + MC_INVALID;
	}

	return cachedEgtValues[egtChannel].load(std::memory_order_relaxed);
}

static void egtRead() {
	if (!driver) {
		efiPrintf("No SPI selected for EGT");
		return;
	}

	for (int i = 0; i < EGT_CHANNEL_COUNT; i++) {
		if (!isChannelConfigured[i]) {
			continue;
		}

		const uint16_t rawValue = cachedEgtValues[i].load(std::memory_order_relaxed);
		max_32855_code code;
		if (tryGetEgtErrorCode(rawValue, code)) {
			efiPrintf("egt%d %s", i, getMcCode(code));
		} else {
			efiPrintf("egt%d %uC", i, rawValue);
		}
	}
}

void initMax31855() {
	for (int i = 0; i < EGT_CHANNEL_COUNT; i++) {
		isChannelConfigured[i] = false;
		csPorts[i] = nullptr;
		csPads[i] = 0;
		cachedEgtValues[i].store(EGT_ERROR_VALUE + MC_INVALID, std::memory_order_relaxed);
	}

	if (!engineConfiguration->max31855enable) {
		driver = nullptr;
		return;
	}

	auto device = engineConfiguration->max31855spiDevice;
	driver = getSpiDevice(device);
	if (!driver) {
		return;
	}

	addConsoleAction("egtinfo", (Void)showEgtInfo);
	addConsoleAction("egtread", (Void)egtRead);

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
			isChannelConfigured[i] = true;
			initChipSelect(i, pin);
			registerBackgroundSpiDevice(max31855Channels[i]);
		}
	}
}

#endif /* EFI_MAX_31855 */
