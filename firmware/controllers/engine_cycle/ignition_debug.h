// ignition_debug.h
#pragma once
#include <stdint.h>

struct IgnitionDebug {
    volatile uint32_t dwell_mask = 0;   // coil i charging (semantic state)
    volatile uint32_t pin_mask   = 0;   // physical MCU output level (post inversion)
};
extern IgnitionDebug gIgnDbg;
