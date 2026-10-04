/*
 ChibiOS/RT - Copyright (C) 2006-2013 Giovanni Di Sirio

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

 http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
 */

#include "pch.h"
#include "chprintf.h"
#include "rusEfiFunctionalTest.h"
#include "flash_int.h"

#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>

#define CONSOLE_WA_SIZE THD_WORKING_AREA_SIZE(4096)

// SIMIA32 starts with interrupts enabled even before the kernel exists. Global
// constructors use critical sections, whose unlock would otherwise reschedule
// through an uninitialized thread. Match the disabled state at MCU startup;
// chSysInit initializes the port and enables scheduling later.
__attribute__((constructor(101))) static void disableInterruptsBeforeConstructors() {
	port_disable();
}

bool main_loop_started = false;

static thread_t* cdtp;

#define cputs(msg) chMsgSend(cdtp, (msg_t)msg)

void printToConsole(const char* p) {
	cputs(p);
}

/*
 * Console print server done using synchronous messages. This makes the access
 * to the C printf() thread safe and the print operation atomic among threads.
 * In this example the message is the zero terminated string itself.
 */
THD_FUNCTION(console_thread, arg) {

	(void)arg;
	while (!chThdShouldTerminateX()) {
		thread_t* tp = chMsgWait();
		puts((char*)chMsgGet(tp));
		fflush(stdout);
		chMsgRelease(tp, MSG_OK);
	}
}

extern int isSerialOverTcpReady;

/**
 * @brief Shell termination handler.
 *
 * @param[in] id event id.
 */
static void termination_handler(eventid_t /*id*/) {

	chThdSleepMilliseconds(10);

	cputs("Init: shell on SD1 terminated");
	chSysLock();
	oqResetI(&SD1.oqueue);
	chSchRescheduleS();
	chSysUnlock();

	// todo: 2nd port for TS

	//  if (shelltp2 && chThdTerminated(shelltp2)) {
	//    chThdWait(shelltp2);
	//    shelltp2 = NULL;
	//    chThdSleepMilliseconds(10);
	//    cputs("Init: shell on SD2 terminated");
	//    chSysLock();
	//    chOQResetI(&SD2.oqueue);
	//    chSysUnlock();
	//  }
}

static event_listener_t sd1fel, sd2fel;

/**
 * @brief SD1 status change handler.
 *
 * @param[in] id event id.
 */
static void sd1_handler(eventid_t id) {
	eventflags_t flags;

	(void)id;
	flags = chEvtGetAndClearFlags(&sd1fel);
	if ((flags & CHN_CONNECTED)) {
		cputs("Init: connection on SD1");
		isSerialOverTcpReady = TRUE;
	}
	if (flags & CHN_DISCONNECTED) {
		cputs("Init: disconnection on SD1");
		isSerialOverTcpReady = FALSE;
		chSysLock();
		iqResetI(&SD1.iqueue);
		chSchRescheduleS();
		chSysUnlock();
	}
}

/**
 * @brief SD2 status change handler.
 *
 * @param[in] id event id.
 */
static void sd2_handler(eventid_t id) {
	eventflags_t flags;

	(void)id;
	flags = chEvtGetAndClearFlags(&sd2fel);
	if ((flags & CHN_CONNECTED)) {
		cputs("Init: connection on SD2");
	}
	if (flags & CHN_DISCONNECTED) {
		cputs("Init: disconnection on SD2");
		chSysLock();
		iqResetI(&SD2.iqueue);
		chSchRescheduleS();
		chSysUnlock();
	}
}

static evhandler_t fhandlers[] = {termination_handler, sd1_handler, sd2_handler};

bool verboseMode = true;

static virtual_timer_t exitTimer;

/*------------------------------------------------------------------------*
 * Simulator main.                                                        *
 *------------------------------------------------------------------------*/
int main(int argc, char** argv) {
	setbuf(stdout, NULL);

	/*
	 * System initializations.
	 * - HAL initialization, this also initializes the configured device drivers
	 *   and performs the board-specific initializations.
	 * - Kernel initialization, the main() function becomes a thread and the
	 *   RTOS is active.
	 */
	halInit();
	chSysInit();

	if (argc == 2) {
		int timeoutSeconds = atoi(argv[1]);
		printf("Running rusEFI simulator for %d seconds, then exiting.\n\n", timeoutSeconds);

		chSysLock();
		chVTSetI(&exitTimer, MY_US2ST(timeoutSeconds * 1e6), [](void*) { exit(0); }, nullptr);
		chSysUnlock();
	}

	/*
	 * Console thread started.
	 */
	cdtp = chThdCreateFromHeap(NULL, CONSOLE_WA_SIZE, "sim", NORMALPRIO + 1, console_thread, NULL);

	/*
	 * Initializing connection/disconnection events.
	 */
	cputs("Shell service started on SD1, SD2");
	cputs("  - Listening for connections on SD1");
	chEvtRegister(chnGetEventSource(&SD1), &sd1fel, 1);
	cputs("  - Listening for connections on SD2");
	chEvtRegister(chnGetEventSource(&SD2), &sd2fel, 2);

	rusEfiFunctionalTest();

	/*
	 * Events servicing loop.
	 */
	while (!chThdShouldTerminateX()) {
		chEvtDispatch(fhandlers, chEvtWaitOne(ALL_EVENTS));
		printPendingMessages();
		chThdSleepMilliseconds(1);
	}

	/*
	 * Clean simulator exit.
	 */
	chEvtUnregister(chnGetEventSource(&SD1), &sd1fel);
	chEvtUnregister(chnGetEventSource(&SD2), &sd2fel);
	return 0;
}

// Each file represents a distinct sector. Configuration verification also reads
// headers, CRC chunks and trailers at offsets within that sector.
static constexpr size_t simulatedFlashSectorSize = 64 * 1024;
static_assert(sizeof(persistent_config_container_s) <= simulatedFlashSectorSize);

uintptr_t getFlashAddrFirstCopy() {
	return simulatedFlashSectorSize;
}

uintptr_t getFlashAddrSecondCopy() {
	return 2 * simulatedFlashSectorSize;
}

flashsector_t intFlashSectorAt(flashaddr_t address) {
	return address / simulatedFlashSectorSize;
}

static bool validFlashRange(flashaddr_t address, size_t size) {
	return address >= getFlashAddrFirstCopy() && address < 3 * simulatedFlashSectorSize &&
		   size <= simulatedFlashSectorSize - address % simulatedFlashSectorSize;
}

static std::string makeFileName(flashaddr_t addr) {
	std::stringstream ss;

	// Keep existing simulator calibration filenames.
	ss << "flash" << unsigned(intFlashSectorAt(addr)) << ".bin";

	return ss.str();
}

int intFlashErase(flashaddr_t address, size_t size) {
	if (!validFlashRange(address, size)) {
		return FLASH_RETURN_NO_PERMISSION;
	}
	std::error_code error;
	std::filesystem::remove(makeFileName(address), error);
	return error ? FLASH_RETURN_OPERROR : FLASH_RETURN_SUCCESS;
}

int intFlashRead(flashaddr_t address, char* buffer, size_t size) {
	if (!validFlashRange(address, size)) {
		return FLASH_RETURN_NO_PERMISSION;
	}
	auto fileName = makeFileName(address);
	std::error_code error;
	bool exists = std::filesystem::exists(fileName, error);
	if (error) {
		return FLASH_RETURN_OPERROR;
	}
	if (!exists) {
		// Only a missing file represents erased flash. An unreadable or
		// truncated copy must not be treated as safe to overwrite.
		memset(buffer, 0xFF, size);
		return FLASH_RETURN_SUCCESS;
	}

	std::ifstream flash(fileName, std::ios::binary);
	flash.seekg(address % simulatedFlashSectorSize);
	flash.read(buffer, size);
	return flash ? FLASH_RETURN_SUCCESS : FLASH_RETURN_OPERROR;
}

int intFlashWrite(flashaddr_t address, const char* buffer, size_t size) {
	// Calibration writes replace the complete image from the start of a sector.
	if (!validFlashRange(address, size) || address % simulatedFlashSectorSize) {
		return FLASH_RETURN_NO_PERMISSION;
	}
	auto fileName = makeFileName(address);
	printf("Simulator: writing config to %s\n", fileName.c_str());

	std::ofstream flash;
	flash.open(fileName, std::ios::binary | std::ios::trunc);

	flash.write(buffer, size);

	flash.close();

	return flash ? FLASH_RETURN_SUCCESS : FLASH_RETURN_OPERROR;
}

static bool compareFlash(flashaddr_t address, const char* expected, size_t size) {
	if (!validFlashRange(address, size)) {
		return false;
	}
	char buffer[128];
	for (size_t offset = 0; offset < size; offset += sizeof(buffer)) {
		size_t count = std::min(sizeof(buffer), size - offset);
		if (intFlashRead(address + offset, buffer, count) != FLASH_RETURN_SUCCESS) {
			return false;
		}
		for (size_t i = 0; i < count; i++) {
			if (buffer[i] != (expected ? expected[offset + i] : char(0xFF))) {
				return false;
			}
		}
	}
	return true;
}

bool intFlashIsErased(flashaddr_t address, size_t size) {
	return compareFlash(address, nullptr, size);
}

bool intFlashCompare(flashaddr_t address, const char* buffer, size_t size) {
	return compareFlash(address, buffer, size);
}

// Write from file in to memory
// void simulatorWriteFlash(const persistent_config_container_s& cfg) {
// 	std::ofstream flash;
// 	flash.open(simFileName, std::ios::binary | std::ios::trunc);

// 	const char* ptr = reinterpret_cast<const char*>(&cfg);
// 	flash.write(ptr, sizeof(cfg));

// 	flash.close();
// }
