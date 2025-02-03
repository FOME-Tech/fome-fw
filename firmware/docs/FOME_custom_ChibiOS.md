FOME is trying to use latest stable ChibiOS with minimal changes

At the moment FOME uses https://github.com/FOME-Tech/ChibiOS/tree/stable_21.11.x.FOME

FOME custom version of ChibiOS has the following changes:

*) LSE auto-detection/fallback, FOME_STM32_LSE_WAIT_MAX/FOME_STM32_LSE_WAIT_MAX_RTCSEL

# TODO items below this line need to be reviewed, seems to NOT be up to date with 21.11 patch

*) minor OS monitoring and maintainability fixes:
   chDbgStackOverflowPanic allows to know which thread has stack overflow
   ON_LOCK_HOOK is used to measure durations of critical sections

*) MMC timeout waitCounter

*) Support for half/timeout-ISR for UART-DMA in /os/hal/include/uart.h and os/hal/ports/STM32/LLD/USARTv1/uart_lld.* by andreika

Weird changes without an explanation:

3) Uncommented "ASMXOBJS" support in os/common/ports/ARMCMx/compilers/GCC/rules.mk

8) Use local "struct tm *t" in os/hal/ports/STM32/LLD/RTCv1/rtc_lld.c

11) Fixes for UART4-UART8, USART_ISR_* flags USART_CR1_M in os/hal/ports/STM32/LLD/USARTv2/serial_lld.c and os/hal/ports/STM32/LLD/USARTv2/uart_lld.c

15) Use QueryPerformanceCounter() instead of POSIX gettimeofday() in os/rt/ports/SIMIA32/chcore.c

New files:
	os/common/startup/SIMIA32/compilers/GCC/rules.mk
	os/hal/ports/simulator/posix/*
