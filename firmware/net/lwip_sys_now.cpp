/**
 * @file lwip_sys_now.cpp
 *
 * Wrap-safe sys_now() for lwIP, see lwip_sys_arch.c
 */

#include "pch.h"

extern "C" uint32_t sys_now() {
	// Truncating a 64-bit millisecond count wraps at exactly 2^32, as lwIP expects
	return (uint32_t)(getTimeNowUs() / 1000);
}
