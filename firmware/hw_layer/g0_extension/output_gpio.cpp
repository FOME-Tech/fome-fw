#include "pch.h"
#include "output_gpio.h"
#include "drivers/gpio/gpio_ext.h"
#include "mpu_util.h"
#include "outputs.h"
#include "service.h"
#include <cmath>

namespace g0 {
namespace {
LockedModule<Outputs> outputs;
class Pwm final : public hardware_pwm {
public:
	size_t index = 0;
	uint32_t frequency = 0;
	float duty = 0;
	bool active = false;
	bool inverted = false;
	void setDuty(float value) override {
		chibios_rt::CriticalSectionLocker lock;
		if (!active || !std::isfinite(value) || value < 0 || value > 1) {
			return;
		}
		duty = value;
		outputs.state.set(index, frequency, uint16_t((inverted ? 1 - duty : duty) * wire::outputDutyMax + 0.5f));
	}
	void setFrequency(float value) override {
		chibios_rt::CriticalSectionLocker lock;
		if (!active) {
			return;
		}
		if (!std::isfinite(value) || value < 1 || value > 500) {
			warning(ObdCode::CUSTOM_ERR_PWM_STATE_ASSERT, "G0 PWM range (1..500 Hz)");
			return;
		}
		frequency = uint32_t(value + 0.5f);
		setDuty(duty);
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
