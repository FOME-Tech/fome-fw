#pragma once
struct GpioChip {
	virtual int init() = 0;
	virtual int setPadMode(size_t, iomode_t) {
		return -1;
	}
	virtual int writePad(size_t, int) {
		return -1;
	}
};
inline GpioChip* testOutputChip = nullptr;
inline int gpiochip_register(brain_pin_e, const char*, GpioChip& chip, size_t) {
	testOutputChip = &chip;
	return 0;
}
