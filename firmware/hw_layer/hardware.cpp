/**
 * @file    hardware.cpp
 * @brief   Hardware package entry point
 *
 * @date May 27, 2013
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"

#include "trigger_input.h"
#include "can_hw.h"
#include "hardware.h"
#include "rtc_helper.h"
#include "bench_test.h"
#include "pin_repository.h"
#include "max31855.h"
#include "smart_gpio.h"
#include "eficonsole.h"
#include "console_io.h"
#include "idle_thread.h"
#include "clutch_state.h"
#include "config_steps.h"

#if EFI_PROD_CODE
#include "mpu_util.h"
#endif /* EFI_PROD_CODE */

#include "idle_hardware.h"
#include "trigger_central.h"
#include "gitversion.h"
#include "vvt.h"
#include "trigger_emulator_algo.h"
#include "boost_control.h"
#include "software_knock.h"
#include "trigger_scope.h"
#include "init.h"
#include "dma_buffers.h"
#if EFI_MC33816
#include "mc33816.h"
#endif /* EFI_MC33816 */

#if EFI_INTERNAL_FLASH
#include "flash_main.h"
#endif

#if HAL_USE_PAL && EFI_PROD_CODE
#include "digital_input_exti.h"
#endif // HAL_USE_PAL

#if HAL_USE_SPI
static void initSpiModules() {
	if (engineConfiguration->is_enabled_spi_1) {
		turnOnSpi(SPI_DEVICE_1);
	}
	if (engineConfiguration->is_enabled_spi_2) {
		turnOnSpi(SPI_DEVICE_2);
	}
	if (engineConfiguration->is_enabled_spi_3) {
		turnOnSpi(SPI_DEVICE_3);
	}
	if (engineConfiguration->is_enabled_spi_4) {
		turnOnSpi(SPI_DEVICE_4);
	}
	if (engineConfiguration->is_enabled_spi_5) {
		turnOnSpi(SPI_DEVICE_5);
	}
	if (engineConfiguration->is_enabled_spi_6) {
		turnOnSpi(SPI_DEVICE_6);
	}
}

#endif

#if HAL_USE_ADC

#ifdef MODULE_MAP_AVERAGING
// What the fast ADC samples for MAP averaging, and where to find it. The fast ADC callback reads these, so
// they're only changed with the system locked, see setFastMapChannels().
static adc_channel_e fastMapChannel = EFI_ADC_NONE;
static adc_channel_e fastMapChannel2 = EFI_ADC_NONE;
// Until the configuration sequence sets them: the fast ADC callback runs from early in startup on some chips
static FastAdcToken fastMapSampleIndex = InvalidFastAdcToken;
static FastAdcToken fastMapSampleIndex2 = InvalidFastAdcToken;
#endif

/**
 * This method is not in the adc* lower-level file because it is more business logic then hardware.
 */
void onFastAdcComplete(adcsample_t*) {
	// this callback is executed 10 000 times a second, it needs to be as fast as possible!
	ScopePerf perf(PE::AdcCallbackFast);

#ifdef MODULE_MAP_AVERAGING
	// The channels actually sampled, not the configured ones: TunerStudio writes a new MAP input before
	// the burn that moves sampling to it
	engine->module<MapAveragingModule>()->submitSample(
			getFastAdc(fastMapSampleIndex) * getAnalogInputDividerCoefficient(fastMapChannel),
			getFastAdc(fastMapSampleIndex2) * getAnalogInputDividerCoefficient(fastMapChannel2));
#endif // MODULE_MAP_AVERAGING
}
#endif /* HAL_USE_ADC */

#ifdef MODULE_MAP_AVERAGING
// Rebuild the fast ADC list from scratch: enableFastAdcChannel() only ever adds a channel
static void setFastMapChannels(adc_channel_e channel, adc_channel_e channel2) {
	chibios_rt::CriticalSectionLocker csl;

	resetFastAdcChannels();

	fastMapSampleIndex = enableFastAdcChannel("Fast MAP 1", channel);
	fastMapSampleIndex2 = enableFastAdcChannel("Fast MAP 2", channel2);
	fastMapChannel = channel;
	fastMapChannel2 = channel2;
}
#endif /* MODULE_MAP_AVERAGING */

void startFastAdcChannels(const engine_configuration_s* prev) {
#ifdef MODULE_MAP_AVERAGING
	auto channel = engineConfiguration->map.sensor.hwChannel;
	auto channel2 = engineConfiguration->map2HwChannel;

	// Leave the fast ADC alone unless a MAP input moved
	if (!prev || channel != fastMapChannel || channel2 != fastMapChannel2) {
		setFastMapChannels(channel, channel2);
	}
#else
	(void)prev;
#endif /* MODULE_MAP_AVERAGING */
}

/**
 * this method is NOT currently invoked on ECU start
 * todo: maybe start invoking this method on ECU start so that peripheral start-up initialization and restart are
 * unified?
 */

void applyNewHardwareSettings() {
	/**
	 * All 'stop' methods need to go before we begin starting pins.
	 *
	 * We take settings from 'activeConfiguration' not 'engineConfiguration' while stopping hardware.
	 * Some hardware is restart unconditionally on change of parameters while for some systems we make extra effort and
	 * restart only relevant settings were changes.
	 *
	 */
	ButtonDebounce::stopConfigurationList();

	stopHardware();

	enginePins.unregisterPins();

	ButtonDebounce::startConfigurationList();

	/*******************************************
	 * Start everything back with new settings *
	 ******************************************/

#if EFI_SHAFT_POSITION_INPUT
	updateTriggerInputPins();
#endif /* EFI_SHAFT_POSITION_INPUT */

	startHardware();

	enginePins.startPins();

#if EFI_PROD_CODE && EFI_IDLE_CONTROL
	if (isIdleHardwareRestartNeeded()) {
		initIdleHardware();
	}
#endif

#if EFI_ENGINE_CONTROL
	startBoostPin();
#endif // EFI_ENGINE_CONTROL
#if EFI_EMULATE_POSITION_SENSORS
	startTriggerEmulatorPins();
#endif /* EFI_EMULATE_POSITION_SENSORS */
#if EFI_VVT_PID
	startVvtControlPins();
#endif /* EFI_VVT_PID */
}

// Weak link a stub so that every board doesn't have to implement this function
__attribute__((weak)) void boardInitHardware() {}

// This function initializes hardware that can do so before configuration is loaded
void initHardwareNoConfig() {
	efiPrintf("initHardware()");

#if EFI_PROD_CODE
	initPinRepository();
#endif

	/**
	 * We need the LED_ERROR pin even before we read configuration
	 */
	initPrimaryPins();

#if EFI_PROD_CODE
	// it's important to initialize this pretty early in the game before any scheduling usages
	initSingleTimerExecutorHardware();
#if EFI_RTC
	initRtc();
#endif // EFI_RTC
#endif // EFI_PROD_CODE

#if EFI_INTERNAL_FLASH
	initFlash();
#endif

#if EFI_SHAFT_POSITION_INPUT
	// todo: figure out better startup logic
	initTriggerCentral();
#endif /* EFI_SHAFT_POSITION_INPUT */

#if HAL_USE_PAL && EFI_PROD_CODE
	// this should be initialized before detectBoardType()
	efiExtiInit();
#endif // HAL_USE_PAL

	boardInitHardware();

#if EFI_PROD_CODE
	dma_buffers::initMpu();
	portInitAdc();
#endif
}

void stopHardware() {
	stopPedalPins();
	stopClutchPins();

#if EFI_EMULATE_POSITION_SENSORS
	stopTriggerEmulatorPins();
#endif /* EFI_EMULATE_POSITION_SENSORS */

#if EFI_VVT_PID
	stopVvtControlPins();
#endif /* EFI_VVT_PID */
}

/**
 * This method is invoked both on ECU start and configuration change
 */
void startHardware() {
#if EFI_SHAFT_POSITION_INPUT
	validateTriggerInputs();
#endif // EFI_SHAFT_POSITION_INPUT

	startPedalPins();
	startClutchPins();
}

void initHardware() {
	if (hasFirmwareError()) {
		return;
	}

#ifdef TRIGGER_SCOPE
	initTriggerScope();
#endif // TRIGGER_SCOPE

#if HAL_USE_SPI
	initSpiModules();
#endif /* HAL_USE_SPI */

#if EFI_PROD_CODE && (BOARD_EXT_GPIOCHIPS > 0)
	// initSmartGpio depends on 'initSpiModules'
	initSmartGpio();
#endif

#if HAL_USE_SPI
	enginePins.sdCsPin.initPin("SD CS", engineConfiguration->sdCardCsPin);
#endif /* HAL_USE_SPI */

#if EFI_ENGINE_CONTROL
	enginePins.startPins();
#endif /* EFI_ENGINE_CONTROL */

#if EFI_MC33816
	initMc33816();
#endif /* EFI_MC33816 */

#if EFI_MAX_31855
	initMax31855();
#endif /* EFI_MAX_31855 */

	// Software knock, CAN, CAN VSS and IMU: see config_steps.cpp
	configStepsBoot(BootPhase::Hardware);

#if EFI_SHAFT_POSITION_INPUT
	updateTriggerInputPins();
#endif /* EFI_SHAFT_POSITION_INPUT */

	startHardware();

	efiPrintf("initHardware() OK!");
}

#if HAL_USE_SPI
// this is F4 implementation but we will keep it here for now for simplicity
int getSpiPrescaler(spi_speed_e speed, spi_device_e device) {
	switch (speed) {
		case _5MHz:
			return device == SPI_DEVICE_1 ? SPI_BaudRatePrescaler_16 : SPI_BaudRatePrescaler_8;
		case _2_5MHz:
			return device == SPI_DEVICE_1 ? SPI_BaudRatePrescaler_32 : SPI_BaudRatePrescaler_16;
		case _1_25MHz:
			return device == SPI_DEVICE_1 ? SPI_BaudRatePrescaler_64 : SPI_BaudRatePrescaler_32;

		case _150KHz:
			// SPI1 does not support 150KHz, it would be 300KHz for SPI1
			return SPI_BaudRatePrescaler_256;
		default:
			// unexpected
			return 0;
	}
}

#endif /* HAL_USE_SPI */
