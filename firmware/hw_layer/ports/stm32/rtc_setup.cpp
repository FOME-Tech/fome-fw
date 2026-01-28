#include "pch.h"

// STM32_RTCSEL_LSE is F4/F7
// STM32_RTCSEL_LSE_CK is H7
#ifndef STM32_RTCSEL_LSE
#define STM32_RTCSEL_LSE STM32_RTCSEL_LSE_CK
#endif

extern "C" void stm32_rtc_init() {
	// Allow backup domain access
	#ifdef STM32F4XX
		PWR->CR |= PWR_CR_DBP;
	#else // not F4
		PWR->CR1 |= PWR_CR1_DBP;
	#endif

	uint32_t lseMode = STM32_LSEDRV | RCC_BDCR_LSEBYP;
	uint32_t lseEnable = RCC_BDCR_LSEON;
	uint32_t rtcSel = STM32_RTCSEL;
	uint32_t rtcEn = RCC_BDCR_RTCEN;

	// Expect LSE configured, enabled, RTC configured, enabled, and LSE ready
	uint32_t bdcrExpected = lseMode | lseEnable | rtcSel | rtcEn | RCC_BDCR_LSERDY;

	if (RCC->BDCR == bdcrExpected) {
		// Already configured correctly, nothing to do.
		return;
	}

	// Reset backup domain
	RCC->BDCR = RCC_BDCR_BDRST;
	RCC->BDCR = 0;

	if (STM32_RTCSEL == STM32_RTCSEL_LSE) {
		// Enable LSE in bypass mode
		RCC->BDCR |= lseMode;
		RCC->BDCR |= lseEnable;

		size_t lseWaitCounter = 0;
		// Waits until LSE is stable or times out.
		while ((RCC->BDCR & RCC_BDCR_LSERDY) == 0) {
			if (lseWaitCounter++ > RUSEFI_STM32_LSE_WAIT_MAX) {
				// LSE timed out, give up and don't configure the RTC
				return;
			}
		}

		if ((RCC->BDCR & RCC_BDCR_RTCEN) == 0) {
			// Select RTC clock source
			RCC->BDCR |= STM32_RTCSEL;

			/* RTC clock enabled.*/
			RCC->BDCR |= RCC_BDCR_RTCEN;
		}
	}
}
