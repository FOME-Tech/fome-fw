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

class AdcDevice {
public:
	explicit AdcDevice(ADCConversionGroup* hwConfig);
	void enableChannel(adc_channel_e hwChannelIndex);
	uint8_t internalAdcIndexByHardwareIndex[EFI_ADC_LAST_CHANNEL];
	int size() const;
	void init();

private:
	ADCConversionGroup* const m_hwConfig;

	/**
	 * Number of ADC channels in use
	 */
	size_t channelCount = 0;
};

#endif /* HAL_USE_ADC */

