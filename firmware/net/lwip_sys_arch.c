/**
 * @file lwip_sys_arch.c
 *
 * ChibiOS' lwIP port, built unmodified except that its sys_now() is renamed out of the way.
 *
 * lwIP compares timeouts with 32-bit modular arithmetic, so sys_now() must be a millisecond
 * counter that wraps at exactly 2^32. ChibiOS derives it by scaling the system time, which at
 * our tick rate wraps after ~23.9 hours: at that point sys_now() would jump backwards and stall
 * every lwIP timer until it caught up again. The replacement lives in lwip_sys_now.cpp.
 */

#define sys_now chibios_sys_now_unused
#include "arch/sys_arch.c"
