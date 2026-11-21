#include "../output_gpio.cpp"
#include <array>
#include <initializer_list>
#include <limits>

namespace g0 {
Link& link() {
	static Link instance;
	return instance;
}
} // namespace g0

static std::array<g0::wire::SetOutputRequest, g0::wire::outputCount> applied;

static void exchange() {
	g0::wire::AppFrame tx = {};
	unsigned count = 0;
	while (g0::outputs.state.request(tx, 10)) {
		assert(++count <= g0::wire::outputCount);
		const auto& sent = tx.setOutputRequest;
		applied[sent.output - 1] = sent;
		g0::wire::AppFrame rx = {};
		rx.outputResponse.frequencyHz = sent.frequencyHz;
		rx.outputResponse.duty = sent.duty;
		g0::outputs.state.response(tx, rx, 10);
	}
}

static void expect(size_t index, uint32_t frequency, uint16_t duty) {
	exchange();
	assert(applied[index].frequencyHz == frequency && applied[index].duty == duty);
}

int main() {
	using namespace g0;
	registerOutputs();
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float inf = std::numeric_limits<float>::infinity();
	assert(!startPwm(Gpio::G0_OUT_1, 501, 0.5f, false));
	assert(!startPwm(Gpio::G0_OUT_1, -1, 0.5f, false));
	assert(!startPwm(Gpio::G0_OUT_1, inf, 0.5f, false));
	auto* pwm = startPwm(Gpio::G0_OUT_1, 100, 0.25f, true);
	assert(pwm);
	pwm->setFrequency(200);
	expect(0, 200, 7500);
	pwm->setDuty(0.6f);
	expect(0, 200, 4000);
	for (float invalid : {-1.0f, 501.0f, inf, -inf}) {
		pwm->setFrequency(invalid);
		assert(pwms[0].frequency == 200);
	}
	assert(testWarnings == 4);
	for (float paused : {0.0f, 0.5f, nan}) {
		pwm->setFrequency(paused);
		expect(0, 0, 10000); // Logical inactive level honors inversion.
		pwm->setDuty(0.25f);
		expect(0, 0, 10000);
		pwm->setFrequency(200);
		expect(0, 200, 7500);
	}

	// Tachometer/speedometer initialization starts paused, then receives frequency.
	auto* tach = startPwm(Gpio::G0_OUT_2, nan, 0.5f, false);
	assert(tach);
	expect(1, 0, 0);
	tach->setFrequency(100);
	expect(1, 100, 5000);

	assert(testOutputChip->writePad(2, 1) == 0);
	expect(2, 0, 10000);
	engine->isPwmEnabled = false;
	updatePwmOutputs(); // Worker must propagate disable without another controller call.
	expect(0, 0, 10000);
	expect(1, 0, 0);
	expect(2, 0, 10000); // Static GPIO is independent of PWM enable.
	pwm->setDuty(0.8f);
	pwm->setFrequency(300);
	expect(0, 0, 10000);
	auto* later = startPwm(Gpio::G0_OUT_4, 50, 0.2f, false);
	assert(later);
	expect(3, 0, 0);
	assert(testOutputChip->setPadMode(1, 0) == 0);
	engine->isPwmEnabled = true;
	updatePwmOutputs();
	expect(0, 300, 2000);
	expect(1, 0, 0); // Deinitialized PWM must not be restored.
	expect(3, 50, 2000);

	assert(testOutputChip->setPadMode(0, 0) == 0);
	pwm->setFrequency(400);
	pwm->setDuty(1);
	updatePwmOutputs();
	outputs.state.disconnected();
	expect(0, 0, 0);
	assert(!pwms[0].active && pwms[0].frequency == 300);
	assert(chibios_rt::CriticalSectionLocker::depth == 0);
}
