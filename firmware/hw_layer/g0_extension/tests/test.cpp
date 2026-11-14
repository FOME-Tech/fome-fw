#include "../analog.h"
#include "../digital.h"
#include "../link.h"
#include "../outputs.h"
#include <cassert>
#include <cstdio>

using namespace g0;

static wire::AppFrame reply(uint8_t command, uint8_t length) {
	wire::AppFrame rx = {};
	rx.responseHeader.command = command;
	rx.responseHeader.payloadLength = length;
	if (command == wire::cmdReadVersion) {
		rx.versionResponse.version = 0x12345678;
		rx.versionResponse.protocolVersion = wire::protocolVersion;
	}
	wire::seal(rx);
	return rx;
}

struct TestModule : Module {
	uint8_t tag = 0;
	int responses = 0;
	int disconnects = 0;
	bool request(wire::AppFrame& tx, uint32_t) override {
		tx.bytes[0] = wire::cmdReadAnalog;
		tx.bytes[33] = tag;
		return true;
	}
	void response(const wire::AppFrame&, const wire::AppFrame&, uint32_t) override {
		responses++;
	}
	void disconnected() override {
		disconnects++;
	}
};

static void testLink() {
	Link link;
	TestModule module;
	assert(link.attach(module));
	assert(link.attach(module));
	wire::AppFrame tx;
	auto rx = reply(wire::cmdReadVersion, wire::versionPayloadLength);
	link.prepare(tx, 0);
	assert(wire::validFrame(tx));
	link.complete(rx, true, 0); // Initial slave frame is unsolicited.
	assert(!link.online());
	link.prepare(tx, 10);
	link.complete(rx, true, 10);
	assert(link.online());
	link.prepare(tx, 20);
	assert(tx.bytes[0] == wire::cmdReadAnalog);
	link.complete(rx, true, 20); // Still the second version request's reply.
	assert(module.responses == 0);
	link.prepare(tx, 30);
	link.complete(reply(wire::cmdReadAnalog, wire::analogPayloadLength), true, 30);
	assert(module.responses == 1);
	link.prepare(tx, 40);
	link.complete(reply(wire::cmdNop, 0), true, 40);
	assert(!link.online() && module.disconnects == 1 && link.errors() == 1);
	link.prepare(tx, 50);
	assert(tx.bytes[0] == wire::cmdReadVersion);
	link.complete({}, false, 50);
	assert(module.disconnects == 2);
	link.prepare(tx, 60);
	link.complete(rx, true, 60);
	link.prepare(tx, 70);
	rx.versionResponse.protocolVersion--;
	wire::seal(rx);
	link.complete(rx, true, 70);
	assert(!link.online());
	rx.versionResponse.protocolVersion = wire::protocolVersion;
	wire::seal(rx);
	link.prepare(tx, 80);
	link.complete(rx, true, 80);
	link.prepare(tx, 90);
	link.complete(rx, true, 90);
	assert(link.online());
	TestModule others[4];
	for (int i = 0; i < 3; i++) {
		assert(link.attach(others[i]));
	}
	assert(!link.attach(others[3]));
}

static void testValidation() {
	wire::AppFrame tx = {};
	tx.indexedRequest = {wire::cmdReadOutput, 2};
	auto rx = reply(wire::cmdReadOutput, wire::outputPayloadLength);
	rx.outputResponse.output = 2;
	wire::seal(rx);
	assert(validResponse(tx, rx));
	for (unsigned bit = 0; bit < wire::appFrameSize * 8; bit++) {
		auto corrupt = rx;
		corrupt.bytes[bit / 8] ^= 1U << (bit % 8);
		assert(!validResponse(tx, corrupt));
	}
	rx.outputResponse.output = 1;
	wire::seal(rx);
	assert(!validResponse(tx, rx));
	rx.outputResponse.output = 2;
	rx.responseHeader.payloadLength++;
	wire::seal(rx);
	assert(!validResponse(tx, rx));
	rx.responseHeader.payloadLength--;
	rx.responseHeader.status = wire::statusUpdateMode;
	wire::seal(rx);
	assert(!validResponse(tx, rx));
	rx.responseHeader.status = wire::statusReady;
	rx.responseHeader.result = wire::resultBusy;
	wire::seal(rx);
	assert(!validResponse(tx, rx));
}

static void testAnalog() {
	Analog analog;
	assert(std::isnan(analog.voltage(0, 0)));
	auto rx = reply(wire::cmdReadAnalog, wire::analogPayloadLength);
	rx.analogResponse.ready = 1;
	rx.analogResponse.channelCount = wire::analogChannelCount;
	for (unsigned i = 0; i < wire::analogChannelCount; i++) {
		rx.analogResponse.millivolts[i] = 1234;
	}
	analog.response({}, rx, UINT32_MAX - 100);
	assert(std::fabs(analog.voltage(11, 0) - 1.234f) < 0.0001f);
	assert(std::isnan(analog.voltage(12, 0)));
	assert(std::isnan(analog.voltage(0, 150)));
	rx.analogResponse.channelCount--;
	analog.response({}, rx, 200);
	assert(std::isnan(analog.voltage(0, 200)));
	rx.analogResponse.channelCount++;
	rx.analogResponse.millivolts[11] = 3301;
	analog.response({}, rx, 210);
	assert(std::isnan(analog.voltage(0, 210)));
	rx.analogResponse.millivolts[11] = 0;
	analog.response({}, rx, 220);
	analog.disconnected();
	assert(std::isnan(analog.voltage(0, 220)));
}

static void testDigital() {
	Digital digital;
	wire::AppFrame tx = {};
	wire::DigitalAllState value;
	assert(!digital.sample(0, 0, value));
	for (unsigned i = 0; i < wire::digitalInputCount; i++) {
		assert(digital.request(tx, 0));
		assert(tx.setInputModeRequest.input == i + 1);
		auto rx = reply(wire::cmdSetInputMode, wire::digitalPayloadLength);
		rx.digitalResponse.mode = wire::digitalMode;
		digital.response(tx, rx, 0);
	}
	digital.request(tx, 10);
	assert(tx.bytes[0] == wire::cmdReadDigitalAll);
	auto rx = reply(wire::cmdReadDigitalAll, wire::digitalAllPayloadLength);
	rx.digitalAllResponse.inputCount = wire::digitalInputCount;
	rx.digitalAllResponse.inputs[3].flags = 3;
	digital.response(tx, rx, 20);
	assert(digital.sample(3, 20, value) && value.flags == 3);
	assert(!digital.sample(4, 20, value));
	assert(!digital.sample(3, 270, value));
	rx.digitalAllResponse.inputCount--;
	digital.response(tx, rx, 30);
	assert(!digital.sample(3, 30, value));
	digital.disconnected();
	digital.request(tx, 40);
	assert(tx.bytes[0] == wire::cmdSetInputMode);
	assert(!digital.setMode(4, InputMode::Digital));
	assert(!digital.setMode(0, InputMode(2)));
}

static void testOutputs() {
	Outputs outputs;
	wire::AppFrame tx = {};
	assert(!outputs.set(4, 0, 0));
	assert(!outputs.set(0, 501, 0));
	assert(!outputs.set(0, 1, 10001));
	// Acknowledge startup off commands.
	for (unsigned i = 0; i < wire::outputCount; i++) {
		assert(outputs.request(tx, 0));
		assert(tx.bytes[0] == wire::cmdDisableOutput);
		outputs.response(tx, {}, 0);
	}
	assert(!outputs.request(tx, 10));
	assert(outputs.set(0, 100, 5000));
	assert(outputs.request(tx, 20));
	assert(tx.setOutputRequest.frequencyHz == 100);
	assert(!outputs.request(tx, 20));  // Same output cannot have two requests in flight.
	assert(outputs.set(0, 200, 2500)); // Producer changes while ACK is in flight.
	wire::AppFrame rx = {};
	rx.outputResponse.frequencyHz = 100;
	rx.outputResponse.duty = 5000;
	outputs.response(tx, rx, 30);
	assert(outputs.request(tx, 40));
	assert(tx.setOutputRequest.frequencyHz == 200 && tx.setOutputRequest.duty == 2500);
	rx.outputResponse.frequencyHz = 200;
	rx.outputResponse.duty = 2500;
	outputs.response(tx, rx, 50);
	assert(!outputs.request(tx, 60));
	outputs.disconnected();
	assert(outputs.request(tx, 70));
}

static void testFairnessAndStall() {
	Link link;
	TestModule modules[3];
	for (uint8_t i = 0; i < 3; i++) {
		modules[i].tag = i;
		assert(link.attach(modules[i]));
	}
	wire::AppFrame tx;
	auto rx = reply(wire::cmdReadVersion, wire::versionPayloadLength);
	link.prepare(tx, 0);
	assert(wire::validFrame(tx));
	link.complete(rx, true, 0);
	link.prepare(tx, 10);
	link.complete(rx, true, 10);
	for (uint32_t tick = 2; tick < 11; tick++) {
		link.prepare(tx, tick * 10);
		assert(tx.bytes[33] == (tick - 2) % 3);
		link.complete(rx, true, tick * 10);
		rx = reply(wire::cmdReadAnalog, wire::analogPayloadLength);
	}
	link.prepare(tx, 400);
	link.complete(rx, true, 400);
	assert(!link.online());
	for (const auto& module : modules) {
		assert(module.disconnects == 1);
	}
	Link idle;
	idle.prepare(tx, 0);
	idle.complete(reply(wire::cmdReadVersion, wire::versionPayloadLength), true, 0);
	rx = reply(wire::cmdReadVersion, wire::versionPayloadLength);
	idle.prepare(tx, 10);
	idle.complete(rx, true, 10);
	idle.prepare(tx, 20);
	assert(tx.bytes[0] == wire::cmdReadVersion);
}

void testBootloader();

int main() {
	testFairnessAndStall();
	testBootloader();
	testOutputs();
	testDigital();
	testAnalog();
	testLink();
	testValidation();
	std::puts("G0 protocol tests passed");
}
