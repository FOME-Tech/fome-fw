/**
 * @file	AdcConfiguration.h
 *
 * @date May 3, 2014
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#pragma once

#if HAL_USE_ADC

#ifndef ADC_MAX_CHANNELS_COUNT
#define ADC_MAX_CHANNELS_COUNT 16
#endif /* ADC_MAX_CHANNELS_COUNT */

#ifndef SLOW_ADC_CHANNEL_COUNT
#ifdef ADC_MUX_PIN
#define SLOW_ADC_CHANNEL_COUNT 32
#else // not ADC_MUX_PIN
#define SLOW_ADC_CHANNEL_COUNT 16
#endif // def ADC_MUX_PIN
#endif // SLOW_ADC_CHANNEL_COUNT

// this structure contains one multi-channel ADC state snapshot
typedef struct {
	volatile adcsample_t adc_data[ADC_MAX_CHANNELS_COUNT];
} adc_state;

class AdcDevice {
public:
	explicit AdcDevice(ADCConversionGroup* hwConfig, adcsample_t *buf, size_t buf_len);
	void enableChannel(adc_channel_e hwChannelIndex);
	adc_channel_e getAdcHardwareIndexByInternalIndex(int index) const;
	uint8_t internalAdcIndexByHardwareIndex[EFI_ADC_LAST_CHANNEL];
	bool isHwUsed(adc_channel_e hwChannel) const;
	int size() const;
	void init();
	uint32_t conversionCount = 0;
	uint32_t errorsCount = 0;
	int getAdcValueByIndex(int internalIndex) const;

	adcsample_t* const m_samples;

	int getAdcValueByHwChannel(adc_channel_e hwChannel) const;

	adc_state values;
private:
	ADCConversionGroup* const m_hwConfig;
	const size_t m_buf_len;

	/**
	 * Number of ADC channels in use
	 */
	size_t channelCount = 0;

	/* STM32 has up-to 4 additional channels routed to internal voltage sources */
	adc_channel_e hardwareIndexByIndernalAdcIndex[ADC_MAX_CHANNELS_COUNT + 4];
};

#endif /* HAL_USE_ADC */

