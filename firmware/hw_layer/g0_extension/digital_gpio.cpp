#include "pch.h"
#include "digital.h"
#include "service.h"
#include "drivers/gpio/gpio_ext.h"

namespace g0 {
namespace {
LockedModule<Digital> digital;
class Inputs final : public GpioChip {
	int init() override {
		return 0;
	}
	int setPadMode(size_t pin, iomode_t mode) override {
		// Pull resistors are determined by the expansion board hardware.
		return pin < wire::digitalInputCount && mode == PAL_MODE_INPUT ? 0 : -1;
	}
	int readPad(size_t pin) override {
		chibios_rt::CriticalSectionLocker lock;
		wire::DigitalAllState value;
		if (!digital.state.sample(pin, nowMs(), value)) {
			return -1;
		}
		return (value.flags & 1) != 0;
	}
};
Inputs inputs;
} // namespace

void registerDigital() {
	efiAssertVoid(
			ObdCode::CUSTOM_ERR_ASSERT,
			gpiochip_register(Gpio::G0_DIGITAL_1, "G0 inputs", inputs, wire::digitalInputCount) >= 0,
			"G0 inputs");
	link().attach(digital);
}
} // namespace g0
