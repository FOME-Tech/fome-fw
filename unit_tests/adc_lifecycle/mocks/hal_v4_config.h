#pragma once

// Host stand-ins for the device registry/CMSIS register constants used by ADCv4.
// Driver structures, error codes and sequence macros come from the real HAL.
#define STM32H7XX
#define STM32_ADC_USE_ADC12 TRUE
#define STM32_ADC_USE_ADC3 TRUE
#define STM32_HAS_ADC1 TRUE
#define STM32_HAS_ADC2 TRUE
#define STM32_HAS_ADC3 TRUE
#define STM32_ADC12_HANDLER testAdc12Handler
#define STM32_ADC3_HANDLER testAdc3Handler
#define STM32_ADC12_NUMBER 18
#define STM32_ADC3_NUMBER 127
#define STM32_ADC_ADC12_DMA_STREAM 0
#define STM32_ADC_ADC3_BDMA_STREAM 0
#define STM32_DMA_IS_VALID_STREAM(stream) TRUE
#define STM32_BDMA_IS_VALID_STREAM(stream) TRUE
#define STM32_DMA_IS_VALID_PRIORITY(priority) TRUE
#define OSAL_IRQ_IS_VALID_PRIORITY(priority) TRUE
#define STM32_ADC_DUAL_MODE TRUE
#define STM32_ADC_SAMPLES_SIZE 32
#define STM32_HCLK 160000000
#define STM32_ADCCLK 40000000
#define STM32_ADCCLK_MAX 50000000
#define ADC_CFGR_EXTEN_0 (1U << 10)
#define ADC_CFGR_EXTSEL_Pos 5
#define ADC_CFGR_OVRMOD (1U << 12)
#define ADC_CFGR_CONT (1U << 13)
#define ADC_CFGR2_OVSR_Pos 16
#define ADC_CFGR2_OVSS_Pos 5
#define ADC_CFGR2_ROVSE 1U
#define TIM_CR2_MMS_1 (1U << 5)
struct ADC_Common_TypeDef;
struct stm32_bdma_stream_t;
