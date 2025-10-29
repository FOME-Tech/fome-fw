#include "pch.h"

#include "hardware.h"
#include "digital_input_exti.h"

/*
 * We need only a small portion of code from FOME codebase in the bootloader.
 * Mostly it's efi_gpio.cpp, flash_main.cpp, etc. needed only to make it work.
 * And stubs needed just to settle down compiler errors.
 * The whole idea of bootloader is to make it as small as possible and reasonably independent.
*/

void chDbgPanic3(const char* /*msg*/, const char* /*file*/, int /*line*/) {
}

void logHardFault(uint32_t /*type*/, uintptr_t /*faultAddress*/, struct port_extctx* /*ctx*/, uint32_t /*csfr*/) { }

void firmwareError(ObdCode /*code*/, const char* /*fmt*/, ...) {
}
namespace priv { void efiPrintfInternal(const char* /*format*/, ...) { } }

// this is supposed to be taken from chconf_common.h but it does not work? I am not sure why :(
// TODO: make this be defined by chconf_common.h?
//#if ! ENABLE_PERF_TRACE
void irqEnterHook() {}
void irqExitHook() {}
void contextSwitchHook() {}
void threadInitHook(void*) {}
void onLockHook() {}
void onUnlockHook() {}
//#endif /* ENABLE_PERF_TRACE */

static ExtiCallback callback;
void extiCallbackThunk(void* data) {
	callback(data, 0);
}

// EXT is not able to give you the front direction but you could read the pin in the callback.
void efiExtiEnablePin(const char *msg, brain_pin_e brainPin, uint32_t mode, ExtiCallback cb, void *cb_data) {
	ioportid_t port = getHwPort(msg, brainPin);
	int index = getHwPin(msg, brainPin);

	ioline_t line = PAL_LINE(port, index);
	palEnableLineEvent(line, mode);
	callback = cb;
	palSetLineCallback(line, extiCallbackThunk, cb_data);
}

void efiExtiDisablePin(brain_pin_e brainPin) {
	ioportid_t port = getHwPort("", brainPin);
	int index = getHwPin("", brainPin);

	ioline_t line = PAL_LINE(port, index);
	palDisableLineEvent(line);
}

void perfEventBegin(PE) { }
void perfEventEnd(PE) { }
void perfEventInstantGlobal(PE) { }
