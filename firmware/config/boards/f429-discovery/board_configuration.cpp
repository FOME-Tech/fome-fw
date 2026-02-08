#include "pch.h"

/**
 * @brief Board-specific initialization code.
 * @todo  Add your board-specific code, if any.
 */
void boardInit() {
	/* NOP */
}

Gpio getRunningLedPin() {
	// LD3 - green
	return Gpio::G13;
}
