#pragma once

#include "rusefi_types.h"

#include "port_mpu_util.h"

#ifdef __cplusplus

// Base MCU
void baseMCUInit(void);
void jump_to_bootloader();
#if EFI_USE_OPENBLT
void jump_to_openblt();
#endif
bool allowFlashWhileRunning();

bool ramReadProbe(volatile const char *read_address);
#if defined(STM32F4)
bool isStm32F42x();
#endif // STM32F4

// ADC
#if HAL_USE_ADC
void portInitAdc();
float getMcuTemperature();
// Convert all slow ADC inputs.  Returns true if the conversion succeeded, false if a failure occured.
bool readSlowAnalogInputs();
// Read the sampled value of a slow channel
adcsample_t getSlowAdcSample(adc_channel_e channel);
#endif

// CAN bus
#if HAL_USE_CAN
bool isValidCanTxPin(brain_pin_e pin);
bool isValidCanRxPin(brain_pin_e pin);
CANDriver* detectCanDevice(brain_pin_e pinRx, brain_pin_e pinTx);
#endif // HAL_USE_CAN

bool isValidSerialTxPin(brain_pin_e pin);
bool isValidSerialRxPin(brain_pin_e pin);

// SPI
#if HAL_USE_SPI
void initSpiModule(SPIDriver *driver, brain_pin_e sck, brain_pin_e miso, brain_pin_e mosi);

void initSpiCs(SPIConfig *spiConfig, brain_pin_e csPin);
void turnOnSpi(spi_device_e device);
#endif // HAL_USE_SPI

#if HAL_USE_ICU
bool getIcuParams(brain_pin_e hwPin, iomode_t *af_ptr, ICUDriver ** icu_ptr, icuchannel_t *channel_ptr, uint32_t *clock_ptr);
#endif

// MMC Card
#if HAL_USE_MMC_SPI
// HS = max 50MHz SPI
extern SPIConfig mmc_hs_spicfg;
// LS = max 25MHz SPI
extern SPIConfig mmc_ls_spicfg;
#endif

// Hardware PWM
struct hardware_pwm {
	static hardware_pwm* tryInitPin(const char* msg, brain_pin_e pin, float frequencyHz, float duty);
	virtual void setDuty(float duty) = 0;
};

// Brownout Reset
typedef enum {
	BOR_Result_Ok = 0x00,
	BOR_Result_Error
} BOR_Result_t;

BOR_Level_t BOR_Get(void);
BOR_Result_t BOR_Set(BOR_Level_t BORValue);

extern "C"
{
#endif /* __cplusplus */

// these need to be declared with C linkage - they're called from C and asm files
void DebugMonitorVector(void);
void UsageFaultVector(void);
void BusFaultVector(void);
void HardFaultVector(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */
