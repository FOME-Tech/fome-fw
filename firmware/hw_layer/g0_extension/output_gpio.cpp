#include "pch.h"
#include "outputs.h"
#include "output_gpio.h"
#include "service.h"
#include "drivers/gpio/gpio_ext.h"
#include "mpu_util.h"
#include <cmath>

namespace g0 {
namespace {
LockedModule<Outputs> outputs;
class Pwm final : public hardware_pwm {
public:
	size_t index = 0;
	uint32_t frequency = 0;
	bool active = false;
	bool inverted = false;
	void setDuty(float duty) override {
		chibios_rt::CriticalSectionLocker lock;
		if (!active || !std::isfinite(duty) || duty < 0 || duty > 1) {
			return;
		}
		outputs.state.set(index, frequency, uint16_t((inverted ? 1 - duty : duty) * wire::outputDutyMax + 0.5f));
	}
};
Pwm pwms[wire::outputCount];

class OutputChip final : public GpioChip {
	int init() override {
		return 0;
	}
	int setPadMode(size_t pin, iomode_t mode) override {
		if (pin >= wire::outputCount) {
			return -1;
		}
		if ((mode & PAL_STM32_MODE_MASK) == PAL_MODE_OUTPUT_PUSHPULL) {
			return 0;
		}
		// efiSetPadUnused requests input/pullup. Always disable the remote driver.
		return writePad(pin, 0);
	}
	int writePad(size_t pin, int value) override {
		if (pin >= wire::outputCount) {
			return -1;
		}
		chibios_rt::CriticalSectionLocker lock;
		pwms[pin].active = false;
		return outputs.state.set(pin, 0, value ? wire::outputDutyMax : 0) ? 0 : -1;
	}
};
OutputChip chip;
} // namespace

bool isOutput(brain_pin_e pin) {
	return pin >= Gpio::G0_OUT_1 && pin <= Gpio::G0_OUT_4;
}

hardware_pwm* startPwm(brain_pin_e pin, float frequency, float duty, bool inverted) {
	if (!isOutput(pin) || !std::isfinite(frequency) || frequency < 1 || frequency > 500 || !std::isfinite(duty) ||
		duty < 0 || duty > 1) {
		return nullptr;
	}
	chibios_rt::CriticalSectionLocker lock;
	auto& pwm = pwms[pin - Gpio::G0_OUT_1];
	pwm.index = pin - Gpio::G0_OUT_1;
	pwm.frequency = uint32_t(frequency + 0.5f);
	pwm.active = true;
	pwm.inverted = inverted;
	pwm.setDuty(duty);
	return &pwm;
}

void registerOutputs() {
	efiAssertVoid(
			ObdCode::CUSTOM_ERR_ASSERT,
			gpiochip_register(Gpio::G0_OUT_1, "G0 lowsides", chip, wire::outputCount) >= 0,
			"G0 outputs");
	link().attach(outputs);
}
} // namespace g0
