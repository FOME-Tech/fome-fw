#pragma once

#include "../../../hw_layer/ports/stm32/stm32f4/cfg/board.h"

// Core8 VR/Hall inputs PE2-PE5 have external pull-ups to 5 V.
// STM32 FT inputs require both internal pulls disabled above VDD + 0.3 V.
// Apply this in the early GPIO configuration, including the bootloader,
// while preserving the default pulls on all other pins.
#undef VAL_GPIOE_PUPDR
#define VAL_GPIOE_PUPDR (VAL_GPIO_PUPDR_ALL_DEFAULT & ~( \
    (3U << (2U * 2U)) | \
    (3U << (3U * 2U)) | \
    (3U << (4U * 2U)) | \
    (3U << (5U * 2U))))
