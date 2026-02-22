#include "pch.h"

extern "C" {
#include "boot.h"
}

#include "hardware.h"
#include "digital_input_exti.h"

/*
 * We need only a small portion of code from FOME codebase in the bootloader.
 * Mostly it's efi_gpio.cpp, flash_main.cpp, etc. needed only to make it work.
 * And stubs needed just to settle down compiler errors.
 * The whole idea of bootloader is to make it as small as possible and reasonably independent.
 */

void chDbgPanic3(const char* /*msg*/, const char* /*file*/, int /*line*/) {}

void logHardFault(uint32_t /*type*/, uintptr_t /*faultAddress*/, struct port_extctx* /*ctx*/, uint32_t /*csfr*/) {}

void firmwareError(ObdCode /*code*/, const char* /*fmt*/, ...) {}
namespace priv {
void efiPrintfInternal(const char* /*format*/, ...) {}
} // namespace priv

void irqEnterHook() {}
void irqExitHook() {}
void contextSwitchHook() {}
void threadInitHook(void*) {}
void onLockHook() {}
void onUnlockHook() {}
void onIdleEnterHook() {}
void onIdleExitHook() {}

static ExtiCallback callback;
void extiCallbackThunk(void* data) {
	callback(data, 0);
}

// EXT is not able to give you the front direction but you could read the pin in the callback.
void efiExtiEnablePin(const char* msg, brain_pin_e brainPin, uint32_t mode, ExtiCallback cb, void* cb_data) {
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

void perfEventBegin(PE) {}
void perfEventEnd(PE) {}
void perfEventInstantGlobal(PE) {}

#if (BOOT_CPU_USER_PROGRAM_START_HOOK > 0)
#include "mmc_card.h"
extern "C" blt_bool CpuUserProgramStartHook(void) {
	// Properly stop the SDMMC peripheral before jumping to the firmware.
	// Without this the bootloader leaves SDMMC1 powered (POWER=3, 24 MHz
	// bypass clock) and the SD card selected; sdcConnect() in the firmware
	// then fails with SDC_COMMAND_TIMEOUT on every command.
	stopMmcBlockDevice();
	return BLT_TRUE;
}
#endif
