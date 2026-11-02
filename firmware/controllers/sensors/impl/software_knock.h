#pragma once

void initSoftwareKnock();
void knockSamplingCallback(uint8_t cylinderIndex, efitick_t nowNt);

#ifdef STM32H7XX
// ADC dual mode (used for the slow ADC on ADC1+ADC2) requires 32-bit adcsample_t, but in dual mode ChibiOS
// configures ADC3's BDMA for 16-bit transfers regardless. Knock sampling runs on ADC3, so its buffer has to be
// 16-bit to match what the DMA actually writes - reading it as adcsample_t would see two samples packed per element.
using knocksample_t = uint16_t;
#else
using knocksample_t = adcsample_t;
#endif

extern knocksample_t knockSampleBuffer[2048];
