#include "../stm32f7ems/efifeatures.h"

#pragma once

#undef EFI_USE_FAST_ADC
// https://github.com/rusefi/rusefi/issues/3301 "H7 is currently actually using fast ADC exclusively - it just needs a bit of plumbing to make it work."
#define EFI_USE_FAST_ADC FALSE

#undef EFI_MC33816
#define EFI_MC33816 FALSE

#undef BOARD_TLE6240_COUNT
#undef BOARD_MC33972_COUNT
#undef BOARD_TLE8888_COUNT
#undef BOARD_L9779_COUNT
#define BOARD_TLE6240_COUNT	0
#define BOARD_MC33972_COUNT	0
#define BOARD_TLE8888_COUNT 	0
#define BOARD_L9779_COUNT 0

#undef EFI_MAX_31855
#define EFI_MAX_31855 FALSE

#define EFI_USE_COMPRESSED_INI_MSD

#undef EFI_EMBED_INI_MSD
#define EFI_EMBED_INI_MSD TRUE

// H7 has dual bank, so flash on its own (low priority) thread so as to not block any other operations
#define EFI_FLASH_WRITE_THREAD TRUE

#undef ENABLE_PERF_TRACE
#define ENABLE_PERF_TRACE TRUE

// H7 runs faster "slow" ADC to make up for reduced oversampling
#define ADC_UPDATE_RATE LoopPeriod::Period1000hz

#undef LUA_USER_HEAP
#define LUA_USER_HEAP 100000
