#include "../link.h"
#include <cassert>
#include <cstdio>

using namespace g0;

static wire::AppFrame reply(uint8_t command, uint8_t length) {
	wire::AppFrame rx = {};
	rx.responseHeader.command = command;
	rx.responseHeader.payloadLength = length;
	return rx;
}

struct TestModule : Module {
	uint8_t tag = 0;
	int responses = 0;
	int disconnects = 0;
	bool request(wire::AppFrame& tx, uint32_t) override {
		tx.bytes[0] = wire::cmdReadAnalog;
		tx.bytes[35] = tag;
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
	rx.versionResponse.version = AppVersion;
	link.prepare(tx, 0);
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
	rx.versionResponse.version = AppVersion - 1;
	link.complete(rx, true, 70);
	assert(!link.online());
	rx.versionResponse.version = AppVersion;
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
	assert(validResponse(tx, rx));
	rx.outputResponse.output = 1;
	assert(!validResponse(tx, rx));
	rx.outputResponse.output = 2;
	rx.responseHeader.payloadLength++;
	assert(!validResponse(tx, rx));
	rx.responseHeader.payloadLength--;
	rx.responseHeader.status = wire::statusUpdateMode;
	assert(!validResponse(tx, rx));
	rx.responseHeader.status = wire::statusReady;
	rx.responseHeader.result = wire::resultBusy;
	assert(!validResponse(tx, rx));
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
	rx.versionResponse.version = AppVersion;
	link.prepare(tx, 0);
	link.complete(rx, true, 0);
	link.prepare(tx, 10);
	link.complete(rx, true, 10);
	for (uint32_t tick = 2; tick < 11; tick++) {
		link.prepare(tx, tick * 10);
		assert(tx.bytes[35] == (tick - 2) % 3);
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
	idle.complete(reply(wire::cmdReadVersion, 4), true, 0);
	rx = reply(wire::cmdReadVersion, 4);
	rx.versionResponse.version = AppVersion;
	idle.prepare(tx, 10);
	idle.complete(rx, true, 10);
	idle.prepare(tx, 20);
	assert(tx.bytes[0] == wire::cmdReadVersion);
}

int main() {
	testFairnessAndStall();
	testLink();
	testValidation();
	std::puts("G0 protocol tests passed");
}
