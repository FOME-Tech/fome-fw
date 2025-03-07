/*
 * @file global.h
 *
 * Global utility header file for firmware
 *
 * Simulator and unit tests have their own version of this header
 *
 * @date May 27, 2013
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#pragma once

#include <hal.h>
// *** IMPORTANT *** from painful experience we know that common_headers.h has to be included AFTER hal.h
// *** https://github.com/rusefi/rusefi/issues/1007 ***
#include "common_headers.h"

// for US_TO_NT_MULTIPLIER
#include "mpu_util.h"

// this is about MISRA not liking 'time.h'. todo: figure out something
#if defined __GNUC__
// GCC
#include <sys/types.h>
#else
// IAR
typedef unsigned int time_t;
#endif

#ifdef __cplusplus
#include "eficonsole.h"
#include <ch.hpp>
#endif /* __cplusplus */


/* definition to expand macro then apply to pragma message */
#define VALUE_TO_STRING(x) #x
#define VALUE(x) VALUE_TO_STRING(x)
#define VAR_NAME_VALUE(var) #var "="  VALUE(var)

/**
 * project-wide default thread stack size
 * See also PORT_INT_REQUIRED_STACK
 * See getRemainingStack()
 * See CountFreeStackSpace()
 * See "threadsinfo" command cmd_threads
 */
#ifndef UTILITY_THREAD_STACK_SIZE
#define UTILITY_THREAD_STACK_SIZE 400
#endif /* UTILITY_THREAD_STACK_SIZE */

/**
 * rusEfi is placing some of data structures into CCM memory simply
 * in order to use that memory - no magic about which RAM is faster etc.
 * That said, CCM/TCM could be faster as there will be less bus contention
 * with DMA.
 *
 * Please note that DMA does not work with CCM memory
 */
#if defined(STM32F4XX)
// CCM memory is 64k
#define CCM_OPTIONAL __attribute__((section(".ram4")))
#define SDRAM_OPTIONAL __attribute__((section(".ram7")))
#define NO_CACHE		// F4 has no cache, do nothing
#define SDMMC_MEMORY(size)	// F4 has no cache, do nothing
#elif defined(STM32F7XX)
// DTCM memory is 128k
#define CCM_OPTIONAL __attribute__((section(".ram3")))
//TODO: update LD file!
#define SDRAM_OPTIONAL __attribute__((section(".ram7")))
// SRAM2 is 16k and set to disable dcache
#define NO_CACHE __attribute__((section(".ram2")))
#define SDMMC_MEMORY(size) NO_CACHE
#elif defined(STM32H7XX)
// DTCM memory is 128k
#define CCM_OPTIONAL __attribute__((section(".ram5")))
//TODO: update LD file!
#define SDRAM_OPTIONAL __attribute__((section(".ram8")))
// SRAM3 is 32k and set to disable dcache
#define NO_CACHE __attribute__((section(".ram3")))
// On H7, SDMMC1 can only talk to AXI, and aligned to the size of the
// object, so its MPU region can disable caching
#define SDMMC_MEMORY(size) __attribute__((section(".ram0")))  __attribute__ ((aligned (size)))
#else /* this MCU doesn't need these */
#define CCM_OPTIONAL
#define NO_CACHE
#define SDMMC_MEMORY
#endif

#define UNIT_TEST_BUSY_WAIT_CALLBACK() {}
