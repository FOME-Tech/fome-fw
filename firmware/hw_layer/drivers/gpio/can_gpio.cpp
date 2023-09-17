#include "pch.h"

#include "gpio/gpio_ext.h"
#include "gpio/can_gpio.h"

class CanGpio : public GpioChip {
public:
	int init() override;
	int setPadMode(size_t pin, iomode_t mode) override;
	int writePad(size_t pin, int value) override;

private:
	uint32_t m_pinsState = 0;
};

int CanGpio::init() {
	return 0;
}

int CanGpio::setPadMode(size_t /*pin*/, iomode_t /*mode*/) {
	return 0;
}

int CanGpio::writePad(size_t pin, int value) {
	if (pin >= 8) {
		return -1;
	}

	{
		chibios_rt::CriticalSectionLocker csl;

		if (value) {
			m_pinsState |=  BIT(pin);
		} else {
			m_pinsState &= ~BIT(pin);
		}
	}

	return 0;
}

static CanGpio chip;

int canGpio_add(Gpio base) {
	return gpiochip_register(base, "CAN", chip, 8);
}
