/**
 * @file efifeatures.h
 *
 * @brief In this header we can configure which firmware modules are used.
 *
 * STM32F7 config is inherited from STM32F4. This file contains only differences between F4 and F7.
 * This is more consistent way to maintain these config 'branches' and add new features.
 *
 * @date Aug 29, 2013
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "../stm32f4/efifeatures.h"

#pragma once

#undef EFI_MC33816
#define EFI_MC33816 FALSE

#define EFI_USE_COMPRESSED_INI_MSD

#undef ENABLE_PERF_TRACE
#define ENABLE_PERF_TRACE TRUE

// F7 may have dual bank, so flash on its own (low priority) thread so as to not block any other operations
#define EFI_FLASH_WRITE_THREAD TRUE

#undef LUA_USER_HEAP
#define LUA_USER_HEAP 200000
