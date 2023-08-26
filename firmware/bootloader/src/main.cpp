
#include "pch.h"
#include "usbconsole.h"
#include "hardware.h"

int main(void) {
	halInit();
	chSysInit();

	baseMCUInit();

	// Set up USB
	usb_serial_start();

	while (true) {
		chThdSleepMilliseconds(1);
	}
}

// very basic version, supports on chip pins only (really only used for USB)
void efiSetPadMode(const char* msg, brain_pin_e brainPin, iomode_t mode) {
	ioportid_t port = getHwPort(msg, brainPin);
	ioportmask_t pin = getHwPin(msg, brainPin);
	/* paranoid */
	if (port == GPIO_NULL) {
		return;
	}

	palSetPadMode(port, pin, mode);
}
