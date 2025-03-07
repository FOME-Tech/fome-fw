/**
 * @file	adc_inputs.cpp
 * @brief	Low level ADC code
 *
 * rusEfi uses two ADC devices on the same 16 pins at the moment. Two ADC devices are used in orde to distinguish between
 * fast and slow devices. The idea is that but only having few channels in 'fast' mode we can sample those faster?
 *
 * At the moment rusEfi does not allow to have more than 16 ADC channels combined. At the moment there is no flexibility to use
 * any ADC pins, only the hardcoded choice of 16 pins.
 *
 * Slow ADC group is used for IAT, CLT, AFR, VBATT etc - this one is currently sampled at 500Hz
 *
 * Fast ADC group is used for MAP, MAF HIP - this one is currently sampled at 10KHz
 *  We need frequent MAP for map_averaging.cpp
 *
 * 10KHz equals one measurement every 3.6 degrees at 6000 RPM
 *
 * @date Jan 14, 2013
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"

float __attribute__((weak)) getAnalogInputDividerCoefficient(adc_channel_e) {
	return engineConfiguration->analogInputDividerCoefficient;
}

#if HAL_USE_ADC

#include "adc_subscription.h"
#include "AdcConfiguration.h"
#include "mpu_util.h"
#include "periodic_thread_controller.h"
#include "protected_gpio.h"

// Board voltage, with divider coefficient accounted for
float getVoltageDivided(const char *msg, adc_channel_e hwChannel) {
	return getVoltage(msg, hwChannel) * getAnalogInputDividerCoefficient(hwChannel);
}

// voltage in MCU universe, from zero to VDD
float getVoltage(const char *msg, adc_channel_e hwChannel) {
	return adcToVolts(getSlowAdcValue(msg, hwChannel));
}

static uint32_t slowAdcCounter = 0;

static float mcuTemperature;

float getMCUInternalTemperature() {
	return mcuTemperature;
}

int getSlowAdcValue(const char *msg, adc_channel_e hwChannel) {
	if (!isAdcChannelValid(hwChannel)) {
		warning(ObdCode::CUSTOM_OBD_ANALOG_INPUT_NOT_CONFIGURED, "ADC: %s input is not configured", msg);
		return -1;
	}

	return getSlowAdcSample(hwChannel);
}

void waitForSlowAdc(uint32_t lastAdcCounter) {
	// we use slowAdcCounter instead of slowAdc.conversionCount because we need ADC_COMPLETE state
	while (slowAdcCounter <= lastAdcCounter) {
		chThdSleepMilliseconds(1);
	}
}

void updateSlowAdc(efitick_t nowNt) {
	{
		ScopePerf perf(PE::AdcConversionSlow);

		if (!readSlowAnalogInputs()) {
			return;
		}

		// Ask the port to sample the MCU temperature
		mcuTemperature = getMcuTemperature();
	}

	{
		ScopePerf perf(PE::AdcProcessSlow);

		slowAdcCounter++;

		AdcSubscription::UpdateSubscribers(nowNt);

		protectedGpio_check(nowNt);
	}
}

#else /* not HAL_USE_ADC */

__attribute__((weak)) float getVoltageDivided(const char*, adc_channel_e) {
	return 0;
}

// voltage in MCU universe, from zero to VDD
__attribute__((weak)) float getVoltage(const char*, adc_channel_e) {
	return 0;
}

#endif
