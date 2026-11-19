#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

enum Gpio {
	B14,
	B15
};
using brain_pin_e = Gpio;
struct SPIDriver {};
struct SPIConfig {};
constexpr unsigned PAL_MODE_OUTPUT_PUSHPULL = 1;
constexpr unsigned PAL_MODE_INPUT_PULLDOWN = 2;
constexpr unsigned PAL_MODE_OUTPUT_OPENDRAIN = 4;
constexpr unsigned PAL_STM32_PUPDR_PULLUP = 8;
inline bool testPins[] = {true, false};
inline bool testPinOutputs[2] = {};
inline std::vector<Gpio> testResetPulses;
inline bool testSelected = false;
inline bool testBusLocked = false;
inline bool testStarted = false;
inline uint32_t testWaited = 0;
inline unsigned testShortSleeps = 0;
inline void chThdSleepMilliseconds(uint32_t ms) {
	testWaited += ms;
	testShortSleeps += ms == 1;
}
inline uint32_t chVTGetSystemTimeX() {
	return testWaited;
}
#define TIME_I2MS(ticks) (ticks)
inline uint32_t testPolledUs = 0;
constexpr uint32_t SystemCoreClock = 480000000;
#define US2RTC(clock, us) ((clock) / 1000000 * (us))
inline void chSysPolledDelayX(uint32_t cycles) {
	testPolledUs += cycles / (SystemCoreClock / 1000000);
}
struct TestLed {
	unsigned toggles = 0;
	void toggle() {
		toggles++;
	}
	void setValue(int) {}
};
inline struct {
	TestLed runningLedPin;
} enginePins;
inline void efiPrintf(const char*, ...) {}
inline unsigned getHwPort(const char*, Gpio) {
	return 0;
}
inline unsigned getHwPin(const char*, Gpio pin) {
	return pin;
}
void palWritePad(unsigned, unsigned pin, bool high);
inline void efiSetPadMode(const char*, Gpio pin, unsigned mode) {
	assert(mode == (pin == Gpio::B14 ? PAL_MODE_OUTPUT_OPENDRAIN | PAL_STM32_PUPDR_PULLUP
								  : PAL_MODE_OUTPUT_PUSHPULL));
	assert(testPins[pin]); // Preload both latches HIGH before enabling either
						   // output driver.
	testPinOutputs[pin] = true;
}
inline void efiSetPadModeWithoutOwnershipAcquisition(const char*, Gpio pin, unsigned mode) {
	assert(pin == Gpio::B15 && mode == PAL_MODE_INPUT_PULLDOWN);
	assert(!testPins[pin] && testPinOutputs[pin]);
	assert(!testSelected && !testStarted && !testBusLocked);
	testPinOutputs[pin] = false;
}
inline void spiAcquireBus(SPIDriver*) {
	assert(!testBusLocked);
	testBusLocked = true;
}
inline void spiReleaseBus(SPIDriver*) {
	assert(testBusLocked);
	testBusLocked = false;
}
inline void spiStart(SPIDriver*, const SPIConfig*) {
	assert(testBusLocked);
	testStarted = true;
}
inline void spiStop(SPIDriver*) {
	assert(!testSelected);
	testStarted = false;
}
inline void spiSelect(SPIDriver*) {
	assert(testStarted);
	testSelected = true;
}
inline void spiUnselect(SPIDriver*) {
	testSelected = false;
}
void spiExchange(SPIDriver*, size_t, const void*, void*);
