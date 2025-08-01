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
#include "logic_analyzer.h"
#include "smart_gpio.h"
#include "accelerometer.h"
#include "eficonsole.h"
#include "console_io.h"
#include "idle_thread.h"
#include "kline.h"

#if EFI_PROD_CODE
#include "mpu_util.h"
#endif /* EFI_PROD_CODE */

#include "AdcConfiguration.h"
#include "idle_hardware.h"
#include "trigger_central.h"
#include "gitversion.h"
#include "vvt.h"
#include "trigger_emulator_algo.h"
#include "boost_control.h"
#include "software_knock.h"
#include "trigger_scope.h"
#include "init.h"
#if EFI_MC33816
#include "mc33816.h"
#endif /* EFI_MC33816 */

#if EFI_INTERNAL_FLASH
#include "flash_main.h"
#endif

#if HAL_USE_PAL && EFI_PROD_CODE
#include "digital_input_exti.h"
#endif // HAL_USE_PAL

#if EFI_CAN_SUPPORT
#include "can_vss.h"
#endif

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

static FastAdcToken fastMapSampleIndex;
static FastAdcToken fastMapSampleIndex2;

/**
 * This method is not in the adc* lower-level file because it is more business logic then hardware.
 */
void onFastAdcComplete(adcsample_t*) {
	// this callback is executed 10 000 times a second, it needs to be as fast as possible!
	ScopePerf perf(PE::AdcCallbackFast);

#ifdef MODULE_MAP_AVERAGING
	engine->module<MapAveragingModule>()->submitSample(
			adcToVoltsDivided(getFastAdc(fastMapSampleIndex), engineConfiguration->map.sensor.hwChannel),
			adcToVoltsDivided(getFastAdc(fastMapSampleIndex2), engineConfiguration->map2HwChannel)
		);
#endif // MODULE_MAP_AVERAGING
}
#endif /* HAL_USE_ADC */

static void calcFastAdcIndexes() {
#if HAL_USE_ADC
	fastMapSampleIndex = enableFastAdcChannel("Fast MAP", engineConfiguration->map.sensor.hwChannel);
	fastMapSampleIndex2 = enableFastAdcChannel("Fast MAP", engineConfiguration->map2HwChannel);
#endif/* HAL_USE_ADC */
}

extern bool isSpiInitialized[6];

void stopSpi(spi_device_e device) {
#if HAL_USE_SPI
	if (!isSpiInitialized[device]) {
		return; // not turned on
	}
	isSpiInitialized[device] = false;
	efiSetPadUnused(getSckPin(device));
	efiSetPadUnused(getMisoPin(device));
	efiSetPadUnused(getMosiPin(device));
#endif /* HAL_USE_SPI */
}

/**
 * this method is NOT currently invoked on ECU start
 * todo: maybe start invoking this method on ECU start so that peripheral start-up initialization and restart are unified?
 */

void applyNewHardwareSettings() {
#if EFI_PROD_CODE
	#if EFI_SHAFT_POSITION_INPUT
		bool allowDangerousHardwareUpdates =
			!engine->rpmCalculator.isRunning();
	#else // not EFI_SHAFT_POSITION_INPUT
		bool allowDangerousHardwareUpdates = true;
	#endif // EFI_SHAFT_POSITION_INPUT
#endif

	/**
	 * All 'stop' methods need to go before we begin starting pins.
	 *
	 * We take settings from 'activeConfiguration' not 'engineConfiguration' while stopping hardware.
	 * Some hardware is restart unconditionally on change of parameters while for some systems we make extra effort and restart only
	 * relevant settings were changes.
	 *
	 */
	ButtonDebounce::stopConfigurationList();

#if EFI_PROD_CODE
	if (allowDangerousHardwareUpdates) {
		stopSensors();
	}
#endif // EFI_PROD_CODE

	stopHardware();

	if (isConfigurationChanged(is_enabled_spi_1)) {
		stopSpi(SPI_DEVICE_1);
	}

	if (isConfigurationChanged(is_enabled_spi_2)) {
		stopSpi(SPI_DEVICE_2);
	}

	if (isConfigurationChanged(is_enabled_spi_3)) {
		stopSpi(SPI_DEVICE_3);
	}

	if (isConfigurationChanged(is_enabled_spi_4)) {
		stopSpi(SPI_DEVICE_4);
	}

	if (isConfigurationChanged(is_enabled_spi_5)) {
		stopSpi(SPI_DEVICE_5);
	}

	if (isConfigurationChanged(is_enabled_spi_6)) {
		stopSpi(SPI_DEVICE_6);
	}

	if (isPinOrModeChanged(clutchUpPin, clutchUpPinMode)) {
		// bug? duplication with stopPedalPins?
		efiSetPadUnused(activeConfiguration.clutchUpPin);
	}

	enginePins.unregisterPins();

#if EFI_PROD_CODE
	reconfigureSensors();
#endif /* EFI_PROD_CODE */

	ButtonDebounce::startConfigurationList();

	/*******************************************
	 * Start everything back with new settings *
	 ******************************************/

#if EFI_PROD_CODE && EFI_SHAFT_POSITION_INPUT
	updateTriggerInputPins();
#endif /* EFI_SHAFT_POSITION_INPUT */

	startHardware();

	enginePins.startPins();

	initKLine();

#if EFI_PROD_CODE && EFI_IDLE_CONTROL
	if (isIdleHardwareRestartNeeded()) {
		 initIdleHardware();
	}
#endif

#if EFI_BOOST_CONTROL
	startBoostPin();
#endif
#if EFI_EMULATE_POSITION_SENSORS
	startTriggerEmulatorPins();
#endif /* EFI_EMULATE_POSITION_SENSORS */
#if EFI_LOGIC_ANALYZER
	startLogicAnalyzerPins();
#endif /* EFI_LOGIC_ANALYZER */
#if EFI_VVT_PID
	startVvtControlPins();
#endif /* EFI_VVT_PID */

	calcFastAdcIndexes();
}

// Weak link a stub so that every board doesn't have to implement this function
__attribute__((weak)) void boardInitHardware() { }
__attribute__((weak)) void setPinConfigurationOverrides() { }

// This function initializes hardware that can do so before configuration is loaded
void initHardwareNoConfig() {
	efiPrintf("initHardware()");

#if EFI_PROD_CODE
	initPinRepository();
#endif

#if EFI_GPIO_HARDWARE
	/**
	 * We need the LED_ERROR pin even before we read configuration
	 */
	initPrimaryPins();
#endif // EFI_GPIO_HARDWARE

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

#if EFI_INTERNAL_ADC
	portInitAdc();
#endif
}

void stopHardware() {
	stopPedalPins();

#if EFI_LOGIC_ANALYZER
	stopLogicAnalyzerPins();
#endif /* EFI_LOGIC_ANALYZER */

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
}

void initHardware() {
	if (hasFirmwareError()) {
		return;
	}

#if EFI_SOFTWARE_KNOCK
	initSoftwareKnock();
#endif /* EFI_SOFTWARE_KNOCK */

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

	// output pins potentially depend on 'initSmartGpio'
	initOutputPins();

#if EFI_ENGINE_CONTROL
	enginePins.startPins();
#endif /* EFI_ENGINE_CONTROL */

#if EFI_MC33816
	initMc33816();
#endif /* EFI_MC33816 */

#if EFI_MAX_31855
	initMax31855(engineConfiguration->max31855spiDevice, engineConfiguration->max31855_cs);
#endif /* EFI_MAX_31855 */

#if EFI_CAN_SUPPORT
	initCan();
#endif /* EFI_CAN_SUPPORT */

#if EFI_PROD_CODE && EFI_SHAFT_POSITION_INPUT
	updateTriggerInputPins();
#endif /* EFI_SHAFT_POSITION_INPUT */

#if EFI_MEMS
	initAccelerometer();
#endif

#if EFI_CAN_SUPPORT
	initCanVssSupport();
#endif // EFI_CAN_SUPPORT

	calcFastAdcIndexes();

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
