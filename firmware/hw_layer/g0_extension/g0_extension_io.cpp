#include "g0_extension_io.h"
#include "g0_extension_io_impl.h"

#if EFI_PROD_CODE

namespace {

class G0ExtensionLowsideHardwarePwm final : public hardware_pwm {
public:
	explicit G0ExtensionLowsideHardwarePwm(size_t outputIndex)
		: m_outputIndex(outputIndex) {}

	void start(float frequencyHz, float duty) {
		m_frequencyHz = frequencyHz < 1 ? 1 : static_cast<uint32_t>(frequencyHz);
		setDuty(duty);
	}

	void setDuty(float duty) override {
		if (duty < 0) {
			duty = 0;
		} else if (duty > 1) {
			duty = 1;
		}

		const auto scaledDuty = static_cast<uint16_t>(duty * g0_extension::OutputDutyMax);
		g0_extension::setLowsidePwm(m_outputIndex, m_frequencyHz, scaledDuty);
	}

private:
	const size_t m_outputIndex;
	uint32_t m_frequencyHz = 1;
};

static G0ExtensionLowsideHardwarePwm g0ExtensionLowsidePwms[g0_extension::OutputCount] = {
		G0ExtensionLowsideHardwarePwm(0),
		G0ExtensionLowsideHardwarePwm(1),
		G0ExtensionLowsideHardwarePwm(2),
		G0ExtensionLowsideHardwarePwm(3),
};

} // namespace

void startG0ExtensionIo() {
	static bool started = false;

	if (started) {
		efiPrintf("G0 extension I/O: already started");
		return;
	}

	started = true;

	g0_extension::startProvider();
	registerAdcProvider(g0_extension::adcProvider(), g0_extension::FirstAdcIndex, g0_extension::AnalogChannelCount);
}

bool readG0ExtensionDigitalInput(size_t idx) {
	return g0_extension::readDigitalInput(idx);
}

bool isG0ExtensionLowsidePin(brain_pin_e pin) {
	return pin >= Gpio::G0_LOWSIDE_0 && pin <= Gpio::G0_LOWSIDE_3;
}

void setG0ExtensionLowsideOutput(brain_pin_e pin, bool value) {
	if (!isG0ExtensionLowsidePin(pin)) {
		return;
	}

	g0_extension::setLowsideOutput(pin - Gpio::G0_LOWSIDE_0, value);
}

void disableG0ExtensionLowsideOutput(brain_pin_e pin) {
	if (!isG0ExtensionLowsidePin(pin)) {
		return;
	}

	g0_extension::disableLowsideOutput(pin - Gpio::G0_LOWSIDE_0);
}

hardware_pwm* tryInitG0ExtensionLowsidePwm(brain_pin_e pin, float frequencyHz, float duty) {
	if (!isG0ExtensionLowsidePin(pin)) {
		return nullptr;
	}

	auto& pwm = g0ExtensionLowsidePwms[pin - Gpio::G0_LOWSIDE_0];
	pwm.start(frequencyHz, duty);
	return &pwm;
}

#else

void startG0ExtensionIo() {}

bool readG0ExtensionDigitalInput(size_t idx) {
	(void)idx;
	return false;
}

bool isG0ExtensionLowsidePin(brain_pin_e pin) {
	return pin >= Gpio::G0_LOWSIDE_0 && pin <= Gpio::G0_LOWSIDE_3;
}

void setG0ExtensionLowsideOutput(brain_pin_e pin, bool value) {
	(void)pin;
	(void)value;
}

void disableG0ExtensionLowsideOutput(brain_pin_e pin) {
	(void)pin;
}

hardware_pwm* tryInitG0ExtensionLowsidePwm(brain_pin_e pin, float frequencyHz, float duty) {
	(void)pin;
	(void)frequencyHz;
	(void)duty;
	return nullptr;
}

#endif
