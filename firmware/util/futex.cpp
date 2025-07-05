#include "futex.h"

inline void fast_mutex_init(fast_mutex_t *m) {
    __atomic_store_n(&m->lock, 0, __ATOMIC_RELEASE);
    chThdQueueObjectInit(&m->queue);
}

inline void fast_mutex_lock(fast_mutex_t *m) {
    uint8_t zero = 0;
    if (__atomic_compare_exchange_n(&m->lock, &zero, 1, false,
                                    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        return;
    }
    chSysLock();
    for (;;) {
        zero = 0;
        if (__atomic_compare_exchange_n(&m->lock, &zero, 1, false,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            chSysUnlock();
            return;
        }
        chThdEnqueueTimeoutS(&m->queue, TIME_INFINITE);
    }
}

inline void fast_mutex_unlock(fast_mutex_t *m) {
    __atomic_store_n(&m->lock, 0, __ATOMIC_RELEASE);
    chSysLock();
    if (!chThdQueueIsEmptyI(&m->queue)) {
        chThdDequeueNextI(&m->queue, MSG_OK);
        chSchRescheduleS();
    }
    chSysUnlock();
}