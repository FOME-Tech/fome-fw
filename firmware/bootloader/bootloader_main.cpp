
#include "pch.h"
#include "usbconsole.h"
#include "hardware.h"
#include "rusefi.h"

extern "C" {
	#include "boot.h"
	#include "shared_params.h"
}

class : public chibios_rt::BaseStaticThread<256> {
protected:
	void main(void) override {
		Gpio yellow = getWarningLedPin();
		Gpio blue = getCommsLedPin();
		Gpio green = getRunningLedPin();

		efiSetPadMode("yellow", yellow, PAL_MODE_OUTPUT_PUSHPULL);
		efiSetPadMode("blue", blue, PAL_MODE_OUTPUT_PUSHPULL);
		efiSetPadMode("green", green, PAL_MODE_OUTPUT_PUSHPULL);

		auto yellowPort = getBrainPinPort(yellow);
		auto yellowPin = getBrainPinIndex(yellow);
		auto bluePort = getBrainPinPort(blue);
		auto bluePin = getBrainPinIndex(blue);
		auto greenPort = getBrainPinPort(green);
		auto greenPin = getBrainPinIndex(green);

		if (yellowPort) {
			palSetPad(yellowPort, yellowPin);
		}
		if (bluePort) {
			palSetPad(bluePort, bluePin);
		}
		if (greenPort) {
			palSetPad(greenPort, greenPin);
		}

		while (true) {
			if (yellowPort) {
				palTogglePad(yellowPort, yellowPin);
			}
			if (bluePort) {
				palTogglePad(bluePort, bluePin);
			}
			if (greenPort) {
				palTogglePad(greenPort, greenPin);
			}
			chThdSleepMilliseconds(250);
		}
	}
} blinky;

class : public chibios_rt::BaseStaticThread<1024> {
	void main() override {
		// Init openblt shared params
		SharedParamsInit();

		// Init openblt itself
		BootInit();

		#if (BOOT_FILE_SYS_ENABLE > 0)
			// Always attempt an SD firmware update (get you unstuck from corrupt firmware)
			FileHandleFirmwareUpdateRequest();
		#endif

		while (true) {
			BootTask();
		}
	}
} openblt;

int main(void) {
	preHalInit();
	halInit();
	chSysInit();

	baseMCUInit();

	// start the blinky thread
	blinky.start(NORMALPRIO + 10);

	// Start openblt on its own thread
	openblt.start(NORMALPRIO + 5);

	while (true) {
		chThdSleepMilliseconds(1);
	}
}

// very basic version, supports on chip pins only (really only used for USB, LEDs)
void efiSetPadMode(const char* msg, brain_pin_e brainPin, iomode_t mode) {
	ioportid_t port = getHwPort(msg, brainPin);
	ioportmask_t pin = getHwPin(msg, brainPin);
	/* paranoid */
	if (!port) {
		return;
	}

	palSetPadMode(port, pin, mode);
}

// Weak linked default implementation (not necessarily required for all boards)
__attribute__((weak)) void preHalInit() { }
