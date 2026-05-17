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

#if EFI_MAX_31855

#define EGT_ERROR_VALUE -1000

static SPIDriver* driver;

static SPIConfig spiConfig[EGT_CHANNEL_COUNT];
static bool channelConfigured[EGT_CHANNEL_COUNT];
static brain_pin_e channelCsBrainPin[EGT_CHANNEL_COUNT];
static ioportid_t csPort[EGT_CHANNEL_COUNT];
static ioportmask_t csPin[EGT_CHANNEL_COUNT];
static NO_CACHE union {
	uint32_t egtPacket[EGT_CHANNEL_COUNT];
	uint8_t egtBytes[EGT_CHANNEL_COUNT][4];
} readBuffer;

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

static bool ensureChannelConfigured(int egtChannel) {
	if (channelConfigured[egtChannel]) {
		return true;
	}

	if (!isBrainPinValid(channelCsBrainPin[egtChannel])) {
		return false;
	}

	csPort[egtChannel] = getHwPort("max31855", channelCsBrainPin[egtChannel]);
	csPin[egtChannel] = getHwPin("max31855", channelCsBrainPin[egtChannel]);
	efiSetPadMode("max31855 CS", channelCsBrainPin[egtChannel], PAL_STM32_MODE_OUTPUT);
	palWritePad(csPort[egtChannel], csPin[egtChannel], PAL_HIGH);
	channelConfigured[egtChannel] = true;

	return true;
}

static uint32_t readEgtPacket(int egtChannel) {
	if (!driver || !ensureChannelConfigured(egtChannel)) {
		return 0xFFFFFFFF;
	}

	spiAcquireBus(driver);
	if (driver->state != SPI_READY || driver->config != &spiConfig[egtChannel]) {
		spiStart(driver, &spiConfig[egtChannel]);
	}
	palWritePad(csPort[egtChannel], csPin[egtChannel], PAL_LOW);

	for (int i = efi::size(readBuffer.egtBytes[egtChannel]) - 1; i >= 0; i--) {
		readBuffer.egtBytes[egtChannel][i] = spiPolledExchange(driver, 0);
	}

	palWritePad(csPort[egtChannel], csPin[egtChannel], PAL_HIGH);
	spiReleaseBus(driver);

	return readBuffer.egtPacket[egtChannel];
}

#define GET_TEMPERATURE_C(x) (((x) >> 18) / 4)

uint16_t getMax31855EgtValue(int egtChannel) {
	uint32_t packet = readEgtPacket(egtChannel);
	max_32855_code code = getResultCode(packet);
	if (code != MC_OK) {
		return EGT_ERROR_VALUE + code;
	} else {
		return GET_TEMPERATURE_C(packet);
	}
}

static void egtRead() {

	if (driver == NULL) {
		efiPrintf("No SPI selected for EGT");
		return;
	}

	efiPrintf("Reading egt");

	uint32_t egtPacket = readEgtPacket(0);

	max_32855_code code = getResultCode(egtPacket);

	efiPrintf("egt %x code=%d %s", (unsigned int)egtPacket, (unsigned int)code, getMcCode(code));

	if (code != MC_INVALID) {
		int refBits = ((egtPacket & 0xFFFF) / 16); // bits 15:4
		float refTemp = refBits / 16.0;
		efiPrintf("reference temperature %.2f", refTemp);

		efiPrintf("EGT temperature %lu", GET_TEMPERATURE_C(egtPacket));
	}
}

void initMax31855(spi_device_e device, egt_cs_array_t max31855_cs) {
#if defined(HW_ATLAS) || (defined(HW_PROTEUS) && defined(STM32H7XX))
	driver = getSpiDevice(SPI_DEVICE_5); // H7 has only SPI5 available for aux peripherals
#else
	driver = getSpiDevice(device);
#endif
	if (driver == NULL) {
		// error already reported
		return;
	}
	// todo:spi device is now enabled separately - should probably be enabled here

	addConsoleAction("egtinfo", (Void)showEgtInfo);

	addConsoleAction("egtread", (Void)egtRead);

	for (int i = 0; i < EGT_CHANNEL_COUNT; i++) {
		channelConfigured[i] = false;
		channelCsBrainPin[i] = max31855_cs[i];

		if (isBrainPinValid(max31855_cs[i])) {
#ifdef STM32H7XX
			spiConfig[i].cfg1 = 7 // 8 bits per byte
							  | SPI_CFG1_MBR_DIV16;
#else
			spiConfig[i].cr1 = getSpiPrescaler(_5MHz, device);
#endif
		}
	}
}

#endif /* EFI_MAX_31855 */
