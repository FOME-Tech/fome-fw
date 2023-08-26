
#include "pch.h"
#include "usbconsole.h"
#include "hardware.h"

int main(void) {
	halInit();
	chSysInit();

	baseMCUInit();

	// set base pin configuration based on the board
	setDefaultBasePins();

	// Set up USB
	usb_serial_start();

	while (true) {
		chThdSleepMilliseconds(1);
	}
}
