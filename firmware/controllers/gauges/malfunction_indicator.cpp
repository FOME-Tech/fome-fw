/**
 * @file malfunction_indicator.cpp
 * @brief We can blink out OBD-II error codes using Malfunction Indicator Light (MIL)
 *
 *
 * @date Dec 20, 2013
 * @author Konstantin Nikonenko
 * @author Andrey Belomutskiy, (c) 2012-2020
 * we show 4 digit error code - 1,5sec * (4xxx+1) digit + 0,4sec * (x3xxx+1) + ....
 * ATTENTION!!! 0 = 1 blink, 1 = 2 blinks, ...., 9 = 10 blinks
 * sequence is the constant!!!
 *
 *
 * This file is part of rusEfi - see http://rusefi.com
 *
 * rusEfi is free software; you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * rusEfi is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 * even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include "pch.h"

#if EFI_MALFUNCTION_INDICATOR
#include "malfunction_central.h"
#include "malfunction_indicator.h"

#include "periodic_thread_controller.h"

#include <charconv>

#define BLINK_HALF_PERIOD 500
#define GAP_BETWEEN_DIGITS_MS 1000
#define GAP_BETWEEN_CODES_MS 5000

static void blink_digit(int digit) {
	for (int i = 0; i < digit; i++) {
		enginePins.checkEnginePin.setValue(1);
		chThdSleepMilliseconds(BLINK_HALF_PERIOD);
		enginePins.checkEnginePin.setValue(0);
		chThdSleepMilliseconds(BLINK_HALF_PERIOD);
	}

	chThdSleepMilliseconds(GAP_BETWEEN_DIGITS_MS);
}

static int char2int(char c) {
	if (c >= '0' && c <= '9') {
		return c - '0';
	} else if (c >= 'a' && c <= 'f') {
		return c - 'a' + 0xa;
	} else if (c >= 'A' && c <= 'F') {
		return c - 'A' + 0xa;
	} else {
		return 0;
	}
}

// display code
static void displayErrorCode(ObdCode code) {
	char buf[8];

	// write it as a hex string
	auto ret = std::to_chars(buf, buf + std::size(buf), (int)code, 16);
	size_t length = ret.ptr - buf;

	for (size_t i = 0; i < length; i++) {
		blink_digit(char2int(buf[i]) + 1);
	}
}

class MILController : public PeriodicController<UTILITY_THREAD_STACK_SIZE> {
public:
	MILController()	: PeriodicController("MFIndicator") { }
private:
	void OnStarted() override {
		// Always do a 3 second blink on boot
		enginePins.checkEnginePin.setValue(1);
		chThdSleepMilliseconds(3000);
	}

	void PeriodicTask(efitick_t) override {
		static error_codes_set_s localErrorCopy;
		getErrorCodes(&localErrorCopy);

		if (localErrorCopy.count) {
			for (int i = 0; i < localErrorCopy.count; i++) {
				chThdSleepMilliseconds(GAP_BETWEEN_CODES_MS);
				displayErrorCode(localErrorCopy.error_codes[i]);
			}
		} else {
			// Turn on the CEL while the engine is stopped
			enginePins.checkEnginePin.setValue(!engine->rpmCalculator.isRunning());
		}
	}
};

static MILController instance;

void initMalfunctionIndicator() {
	instance.setPeriod(10 /*ms*/);
	instance.start();
}

#endif /* EFI_MALFUNCTION_INDICATOR */
