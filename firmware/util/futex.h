#pragma once
#include "hal.h"
#include "ch.h"
#include <cstdint>

struct fast_mutex_t {
    // 0 == unlocked, 1 == locked
    volatile uint8_t      lock;
    threads_queue_t       queue;
};

void fast_mutex_unlock(fast_mutex_t *m);
void fast_mutex_lock(fast_mutex_t *m);
void fast_mutex_init(fast_mutex_t *m);