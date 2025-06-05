/*
 * @file boards.h
 *
 * @date Nov 15, 2013
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#pragma once

#include "engine_configuration.h"
#include "adc_math.h"

#define ADC_CHANNEL_VREF 0

int getAdcValue(const char *msg, adc_channel_e channel);


// via: ChibiOS/os/common/ports/templates/chcore.h
/**
 * @brief   Interrupt saved context.
 * @details This structure represents the stack frame saved during a
 *          preemption-capable interrupt handler.
 * @note    R2 and R13 are not saved because those are assumed to be immutable
 *          during the system life cycle.
 */
struct port_extctx {
};
