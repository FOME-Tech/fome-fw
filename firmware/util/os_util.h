/*
 * @file    rfiutil.h
 * @brief   Number to string conversion header
 *
 * @date Nov 15, 2012
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#pragma once

#include "global.h"

#if !EFI_UNIT_TEST

/**
 * Unfortunately ChibiOS has two versions of methods for different
 * contexts.
 */
#define isLocked() (ch0.dbg.lock_cnt > 0)

void chVTSetAny(virtual_timer_t *vtp, systime_t time, vtfunc_t vtfunc, void *par);

#endif /* EFI_UNIT_TEST */
