#include "../output_gpio.cpp"
#include <initializer_list>
#include <limits>

namespace g0 {
Link& link() {
	static Link instance;
	return instance;
}
} // namespace g0

int main() {
	using namespace g0;
	registerOutputs();
	assert(!startPwm(Gpio::G0_OUT_1, 501, 0.5f, false));
	auto* pwm = startPwm(Gpio::G0_OUT_1, 100, 0.25f, true);
	assert(pwm);
	pwm->setFrequency(200);
	wire::AppFrame tx = {};
	assert(outputs.state.request(tx, 0));
	assert(tx.setOutputRequest.output == 1);
	assert(tx.setOutputRequest.frequencyHz == 200);
	assert(tx.setOutputRequest.duty == 7500);
	pwm->setDuty(0.6f);
	assert(pwms[0].duty == 0.6f && pwms[0].frequency == 200);
	for (float invalid : {0.0f, 501.0f, std::numeric_limits<float>::quiet_NaN()}) {
		pwm->setFrequency(invalid);
		assert(pwms[0].frequency == 200);
	}
	assert(testWarnings == 3);
	assert(testOutputChip->setPadMode(0, 0) == 0);
	assert(!pwms[0].active);
	pwm->setFrequency(300);
	pwm->setDuty(1);
	assert(!pwms[0].active && pwms[0].frequency == 200);
	outputs.state.disconnected();
	assert(outputs.state.request(tx, 10));
	// The scheduler rotates; inspect the disabled channel after startup commands.
	for (unsigned i = 0; tx.indexedRequest.index != 1 && i < wire::outputCount; i++) {
		outputs.state.response(tx, {}, 10);
		assert(outputs.state.request(tx, 10));
	}
	assert(tx.indexedRequest.index == 1 && tx.bytes[0] == wire::cmdDisableOutput);
	assert(chibios_rt::CriticalSectionLocker::depth == 0);
}
