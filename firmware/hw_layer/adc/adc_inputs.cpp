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

static AdcChannelMode adcHwChannelEnabled[HW_MAX_ADC_INDEX];

// Board voltage, with divider coefficient accounted for
float getVoltageDivided(const char *msg, adc_channel_e hwChannel) {
	return getVoltage(msg, hwChannel) * getAnalogInputDividerCoefficient(hwChannel);
}

// voltage in MCU universe, from zero to VDD
float getVoltage(const char *msg, adc_channel_e hwChannel) {
	return adcToVolts(getAdcValue(msg, hwChannel));
}

#if EFI_USE_FAST_ADC
AdcDevice::AdcDevice(ADCConversionGroup* hwConfig, adcsample_t *buf, size_t buf_len)
	: m_samples(buf)
	, m_hwConfig(hwConfig)
	, m_buf_len(buf_len)
{
	m_hwConfig->sqr1 = 0;
	m_hwConfig->sqr2 = 0;
	m_hwConfig->sqr3 = 0;
#if ADC_MAX_CHANNELS_COUNT > 16
	m_hwConfig->sqr4 = 0;
	m_hwConfig->sqr5 = 0;
#endif /* ADC_MAX_CHANNELS_COUNT */
	memset(hardwareIndexByIndernalAdcIndex, EFI_ADC_NONE, sizeof(hardwareIndexByIndernalAdcIndex));
	memset(internalAdcIndexByHardwareIndex, 0xFF, sizeof(internalAdcIndexByHardwareIndex));
}

#endif // EFI_USE_FAST_ADC

static uint32_t slowAdcCounter = 0;

static adcsample_t getAvgAdcValue(int index, adcsample_t *samples, int bufDepth, int numChannels) {
	uint32_t result = 0;
	for (int i = 0; i < bufDepth; i++) {
		result += samples[index];
		index += numChannels;
	}

	// this truncation is guaranteed to not be lossy - the average can't be larger than adcsample_t
	return static_cast<adcsample_t>(result / bufDepth);
}


// See https://github.com/rusefi/rusefi/issues/976 for discussion on this value
#define ADC_SAMPLING_FAST ADC_SAMPLE_28

#if EFI_USE_FAST_ADC
extern AdcDevice fastAdc;
#endif // EFI_USE_FAST_ADC

static float mcuTemperature;

float getMCUInternalTemperature() {
	return mcuTemperature;
}

int getInternalAdcValue(const char *msg, adc_channel_e hwChannel) {
	if (!isAdcChannelValid(hwChannel)) {
		warning(ObdCode::CUSTOM_OBD_ANALOG_INPUT_NOT_CONFIGURED, "ADC: %s input is not configured", msg);
		return -1;
	}

#if EFI_USE_FAST_ADC
	if (adcHwChannelEnabled[hwChannel] == AdcChannelMode::Fast) {
		int internalIndex = fastAdc.internalAdcIndexByHardwareIndex[hwChannel];
// todo if ADC_BUF_DEPTH_FAST EQ 1
//		return fastAdc.samples[internalIndex];
		int value = getAvgAdcValue(internalIndex, fastAdc.m_samples, ADC_BUF_DEPTH_FAST, fastAdc.size());
		return value;
	}
#endif // EFI_USE_FAST_ADC

	return getSlowAdcSample(hwChannel);
}

#if EFI_USE_FAST_ADC

int AdcDevice::size() const {
	return channelCount;
}

int AdcDevice::getAdcValueByHwChannel(adc_channel_e hwChannel) const {
	int internalIndex = internalAdcIndexByHardwareIndex[hwChannel];
	return values.adc_data[internalIndex];
}

int AdcDevice::getAdcValueByIndex(int internalIndex) const {
	return values.adc_data[internalIndex];
}

void AdcDevice::init() {
	m_hwConfig->num_channels = size();
	/* driver does this internally */
	//hwConfig->sqr1 += ADC_SQR1_NUM_CH(size());
}

bool AdcDevice::isHwUsed(adc_channel_e hwChannelIndex) const {
	for (size_t i = 0; i < channelCount; i++) {
		if (hardwareIndexByIndernalAdcIndex[i] == hwChannelIndex) {
			return true;
		}
	}
	return false;
}

void AdcDevice::enableChannel(adc_channel_e hwChannel) {
	if ((channelCount + 1) >= ADC_MAX_CHANNELS_COUNT) {
		firmwareError(ObdCode::OBD_PCM_Processor_Fault, "Too many ADC channels configured");
		return;
	}

	// hwChannel = which external pin are we using
	// adcChannelIndex = which ADC channel are we using
	// adcIndex = which index does that get in sampling order
	size_t adcChannelIndex = hwChannel - EFI_ADC_0;
	size_t adcIndex = channelCount++;

	internalAdcIndexByHardwareIndex[hwChannel] = adcIndex;
	hardwareIndexByIndernalAdcIndex[adcIndex] = hwChannel;

	if (adcIndex < 6) {
		m_hwConfig->sqr3 |= adcChannelIndex << (5 * adcIndex);
	} else if (adcIndex < 12) {
		m_hwConfig->sqr2 |= adcChannelIndex << (5 * (adcIndex - 6));
	} else if (adcIndex < 18) {
		m_hwConfig->sqr1 |= adcChannelIndex << (5 * (adcIndex - 12));
	}
}

adc_channel_e AdcDevice::getAdcHardwareIndexByInternalIndex(int index) const {
	return hardwareIndexByIndernalAdcIndex[index];
}

#endif // EFI_USE_FAST_ADC

static void printAdcValue(int channel) {
	int value = getAdcValue("print", (adc_channel_e)channel);
	float volts = adcToVoltsDivided(value, (adc_channel_e)channel);
	efiPrintf("adc voltage : %.2f", volts);
}

void waitForSlowAdc(uint32_t lastAdcCounter) {
	// we use slowAdcCounter instead of slowAdc.conversionCount because we need ADC_COMPLETE state
	// todo: use sync.objects?
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

static void addFastAdcChannel(const char* /*name*/, adc_channel_e setting) {
	if (!isAdcChannelValid(setting)) {
		return;
	}

	adcHwChannelEnabled[setting] = AdcChannelMode::Fast;

#if EFI_USE_FAST_ADC
	fastAdc.enableChannel(setting);
#endif
}

static void removeFastAdcChannel(const char *name, adc_channel_e setting) {
	(void)name;
	if (!isAdcChannelValid(setting)) {
		return;
	}

	adcHwChannelEnabled[setting] = AdcChannelMode::Off;
}

void initAdcInputs() {
	efiPrintf("initAdcInputs()");

	memset(adcHwChannelEnabled, 0, sizeof(adcHwChannelEnabled));

	addFastAdcChannel("MAP", engineConfiguration->map.sensor.hwChannel);

#if EFI_INTERNAL_ADC
	portInitAdc();

#if EFI_USE_FAST_ADC
	fastAdc.init();
#endif // EFI_USE_FAST_ADC

	addConsoleActionI("adc", (VoidInt) printAdcValue);
#endif
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
