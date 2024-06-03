/**
 * @file efitime.h
 *
 * By the way, there are 86400000 milliseconds in a day
 *
 * @date Apr 14, 2014
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#pragma once

#include "efifeatures.h"
#include "rusefi_types.h"
#include "global.h"

#if EFI_PROD_CODE
// for US_TO_NT_MULTIPLIER which is port-specific
#include "port_mpu_util.h"
#endif

#define MS_PER_SECOND 1000
#define US_PER_SECOND 1000000
#define US_PER_SECOND_F 1000000.0
#define US_PER_SECOND_LL 1000000LL
#define NT_PER_SECOND (US2NT(US_PER_SECOND_LL))

#define MS2US(MS_TIME) ((MS_TIME) * 1000)
#define US2MS(US_TIME) ((US_TIME) / 1000)

// microseconds to ticks
// since only about 20 seconds of ticks fit in 32 bits this macro is casting parameter into 64 bits 'efitick_t' type
// please note that int64 <-> float is a heavy operation thus we have 'USF2NT' below
#define US2NT(us) (efidur_t{(((efidur_t::rep)(us)) * US_TO_NT_MULTIPLIER)})

// microseconds to ticks, but floating point
// If converting a floating point time period, use this macro to avoid
// the expensive conversions from int64 <-> float
#define USF2NT(us_float) ((us_float) * US_TO_NT_MULTIPLIER)
#define USF2MS(us_float) (0.001f * (us_float))

// And back
#define NT2US(x) ((x).count() / US_TO_NT_MULTIPLIER)
#define NT2USF(x) (((float)(x)) / US_TO_NT_MULTIPLIER)

// milliseconds to ticks
#define MS2NT(msTime) US2NT(MS2US(msTime))
// See USF2NT above for when to use MSF2NT
#define MSF2NT(msTimeFloat) USF2NT(MS2US(msTimeFloat))

#ifdef __cplusplus
/**
 * Provide a 62-bit counter from a 32-bit counter source that wraps around.
 *
 * If you'd like it use it with a 16-bit counter, shift the source by 16 before passing it here.
 * This class is thread/interrupt-safe.
 */
struct WrapAround62 {
	uint64_t update(uint32_t source) {
		// Shift cannot be 31, as we wouldn't be able to tell if time is moving forward or
		// backward relative to m_upper.  We do need to handle both directions as our
		// "thread" can be racing with other "threads" in sampling stamp and updating
		// m_upper.
		constexpr unsigned shift = 30;

		uint32_t upper = m_upper;
		uint32_t relative_unsigned = source - (upper << shift);
		upper += int32_t(relative_unsigned) >> shift;
		m_upper = upper;

		// Yes we could just do upper<<shift, but then the result would span both halves of
		// the 64-bit result.  Doing it this way means we only operate on one half at a
		// time.  Source will supply those bits anyways, so we don't need them from
		// upper...
		return (int64_t(upper >> (32 - shift)) << 32) | source;
	}

private:
	volatile uint32_t m_upper = 0;
};

/**
 * Get a monotonically increasing (but wrapping) 32-bit timer value
 * Implemented at port level, based on timer or CPU tick counter
 * Main source of EFI clock, SW-extended to 64bits
 */
uint32_t getTimeNowLowerNt();

/**
 * 64-bit counter CPU/timer cycles since MCU reset
 *
 * See getTimeNowLowerNt for a quicker version which returns only lower 32 bits
 * Lower 32 bits are enough if all we need is to measure relatively short time durations
 * (BTW 2^32 cpu cycles at 168MHz is 25.59 seconds)
 */
efitick_t getTimeNowNt();

/**
 * 64-bit counter of microseconds (1/1 000 000 of a second) since MCU reset
 *
 * By using 64 bit, we can achieve a very precise timestamp which does not overflow.
 * The primary implementation counts the number of CPU cycles from MCU reset.
 *
 * WARNING: you should use getTimeNowNt where possible for performance reasons.
 * The heaviest part is '__aeabi_ildivmod' - non-native 64 bit division
 */
efitimeus_t getTimeNowUs();

/**
 * @brief   Current system time in seconds.
 */
int64_t getTimeNowS();

#if EFI_UNIT_TEST
// In unit tests, we can time travel...
void setTimeNowUs(int us);
void advanceTimeUs(int us);
#endif

#endif /* __cplusplus */
