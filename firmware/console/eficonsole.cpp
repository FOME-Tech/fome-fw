/**
 * @file    eficonsole.cpp
 * @brief   Console package entry point code
 *
 *
 * @date Nov 15, 2012
 * @author Andrey Belomutskiy, (c) 2012-2020
 *
 *
 * This file is part of rusEfi - see http://rusefi.com
 *
 * rusEfi is free software; you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * rusEfi is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 * even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include "pch.h"


#include "eficonsole.h"
#include "console_io.h"
#include "gitversion.h"

static void testCritical() {
	chDbgCheck(0);
}

static void myerror() {
	firmwareError(ObdCode::CUSTOM_ERR_TEST_ERROR, "firmwareError: %d", getRusEfiVersion());
}

static void sayHello() {
	efiPrintf(PROTOCOL_HELLO_PREFIX " rusEFI LLC (c) 2012-2023. All rights reserved.");
	efiPrintf(PROTOCOL_HELLO_PREFIX " built from " GIT_HASH);
	efiPrintf(PROTOCOL_HELLO_PREFIX " FOME v%d", getRusEfiVersion());
	efiPrintf(PROTOCOL_HELLO_PREFIX " Chibios Kernel:       %s", CH_KERNEL_VERSION);
	efiPrintf(PROTOCOL_HELLO_PREFIX " Compiled:     " __DATE__ " - " __TIME__ "");
	efiPrintf(PROTOCOL_HELLO_PREFIX " COMPILER=%s", __VERSION__);
#if EFI_USE_OPENBLT
	efiPrintf(PROTOCOL_HELLO_PREFIX " with OPENBLT");
#endif

#if ENABLE_AUTO_DETECT_HSE
	extern float hseFrequencyMhz;
	extern uint8_t autoDetectedRoundedMhz;
	efiPrintf(PROTOCOL_HELLO_PREFIX " detected HSE clock %.2f MHz PLLM = %d", hseFrequencyMhz, autoDetectedRoundedMhz);
#endif /* ENABLE_AUTO_DETECT_HSE */

	efiPrintf("hellenBoardId=%d", engine->engineState.hellenBoardId);

#if defined(STM32F4) || defined(STM32F7) || defined(STM32H7)
	uint32_t *uid = ((uint32_t *)UID_BASE);
	efiPrintf("UID=%x %x %x", (unsigned int)uid[0], (unsigned int)uid[1], (unsigned int)uid[2]);

	efiPrintf("can read 0x20000010 %d", ramReadProbe((const char *)0x20000010));
	efiPrintf("can read 0x20020010 %d", ramReadProbe((const char *)0x20020010));
	efiPrintf("can read 0x20070010 %d", ramReadProbe((const char *)0x20070010));

#if defined(STM32F4)
	efiPrintf("isStm32F42x %s", boolToString(isStm32F42x()));
#endif // STM32F4

#define 	TM_ID_GetFlashSize()    (*(__IO uint16_t *) (FLASHSIZE_BASE))
#define MCU_REVISION_MASK  0xfff

	int mcuRevision = DBGMCU->IDCODE & MCU_REVISION_MASK;

#ifndef MIN_FLASH_SIZE
#define MIN_FLASH_SIZE 1024
#endif // MIN_FLASH_SIZE

	int flashSize = TM_ID_GetFlashSize();
	if (flashSize < MIN_FLASH_SIZE) {
		firmwareError(ObdCode::OBD_PCM_Processor_Fault, "Expected at least %dK of flash but found %dK", MIN_FLASH_SIZE, flashSize);
	}

	// todo: bug, at the moment we report 1MB on dual-bank F7
	efiPrintf("MCU rev=%x flashSize=%d", mcuRevision, flashSize);
#endif

	/**
	 * Time to finish output. This is needed to avoid mix-up of this methods output and console command confirmation
	 */
	chThdSleepMilliseconds(5);
}

void validateStack(const char*msg, ObdCode code, int desiredStackUnusedSize) {
#if CH_DBG_THREADS_PROFILING && CH_DBG_FILL_THREADS
	int unusedStack = CountFreeStackSpace(chThdGetSelfX()->wabase);
	if (unusedStack < desiredStackUnusedSize) {
		warning(code, "Stack low on %s: %d", msg, unusedStack);
	}
#else
	(void)msg; (void)code; (void)desiredStackUnusedSize;
#endif
}

#if CH_DBG_THREADS_PROFILING && CH_DBG_FILL_THREADS
int CountFreeStackSpace(const void* wabase) {
	const uint8_t* stackBase = reinterpret_cast<const uint8_t*>(wabase);
	const uint8_t* stackUsage = stackBase;

	// thread stacks are filled with CH_DBG_STACK_FILL_VALUE
	// find out where that ends - that's the last thing we needed on the stack
	while (*stackUsage == CH_DBG_STACK_FILL_VALUE) {
		stackUsage++;
	}

	return (int)(stackUsage - stackBase);
}
#endif

/**
 * This methods prints all threads, their stack usage, and their total times
 */
static void cmd_threads() {
#if CH_DBG_THREADS_PROFILING && CH_DBG_FILL_THREADS

	thread_t* tp = chRegFirstThread();

	efiPrintf("name\twabase\ttime\tfree stack");

	while (tp) {
		int freeBytes = CountFreeStackSpace(tp->wabase);
		efiPrintf("%s\t%08x\t%lu\t%d", tp->name, (unsigned int)tp->wabase, tp->time, freeBytes);

		if (freeBytes < 64) {
			firmwareError(ObdCode::OBD_PCM_Processor_Fault, "Ran out of stack on thread %s, %d bytes remain", tp->name, freeBytes);
		}

		tp = chRegNextThread(tp);
	}

	int isrSpace = CountFreeStackSpace(reinterpret_cast<void*>(0x20000000));
	efiPrintf("isr\t0\t0\t%d", isrSpace);

#else // CH_DBG_THREADS_PROFILING && CH_DBG_FILL_THREADS

  efiPrintf("CH_DBG_THREADS_PROFILING && CH_DBG_FILL_THREADS is not enabled");

#endif
}

void initializeConsole() {
	initConsoleLogic();

	startConsole(&handleConsoleLine);

	sayHello();
	addConsoleAction("test", [](){ /* do nothing */});
	addConsoleAction("hello", sayHello);

	addConsoleAction("critical", testCritical);
	addConsoleAction("error", myerror);
	addConsoleAction("threadsinfo", cmd_threads);
}
