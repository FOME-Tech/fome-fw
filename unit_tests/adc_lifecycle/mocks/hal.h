#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>

#define TRUE 1
#define FALSE 0
#define HAL_USE_ADC TRUE
#define ADC_USE_WAIT FALSE
#define ADC_USE_MUTUAL_EXCLUSION FALSE
#ifdef ADC_TEST_V4
#include "hal_v4_config.h"
#else
#define STM32F4XX
#define STM32_ADC_USE_ADC1 TRUE
#define STM32_ADC_USE_ADC2 TRUE
#define STM32_ADC_USE_ADC3 TRUE
#define STM32_HAS_ADC1 TRUE
#define STM32_HAS_ADC2 TRUE
#define STM32_HAS_ADC3 TRUE
#define STM32_DMA_IS_VALID_ID(stream, mask) TRUE
#define STM32_PCLK2 84000000
#define STM32_ADC_ADCPRE 1
#define ADC_CR2_SWSTART (1U << 30)
#endif
struct ADC_TypeDef;
struct stm32_dma_stream_t;

// Use the vendored HAL's state machine, structures and port-specific register macros.
#include "hal_adc.h"

inline int lockDepth = 0;
inline void osalSysLock() {
	++lockDepth;
}
inline void osalSysUnlock() {
	assert(lockDepth > 0);
	--lockDepth;
}
#define osalDbgCheckClassI() assert(lockDepth > 0)
#define osalDbgCheck(condition) assert(condition)
#define osalDbgAssert(condition, message) assert((condition) && message)

using msg_t = int;
constexpr msg_t MSG_OK = 0;
inline msg_t adcConvert(ADCDriver*, const ADCConversionGroup*, adcsample_t*, size_t) {
	return MSG_OK;
}
