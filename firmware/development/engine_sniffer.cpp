/**
 * @file	engine_sniffer.cpp
 * @brief	rusEfi console wave sniffer logic
 *
 * Here we have our own build-in logic analyzer. The data we aggregate here is sent to the
 * java UI rusEfi Console so that it can be displayed nicely in the Sniffer tab.
 *
 * Both external events (see logic_analyzer.cpp) and internal (see signal executors) are supported
 *
 * @date Jun 23, 2013
 * @author Andrey Belomutskiy, (c) 2012-2020
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

#include "engine_sniffer.h"

#if EFI_ENGINE_SNIFFER
#define addEngineSnifferEvent(name, msg) { if (getTriggerCentral()->isEngineSnifferEnabled) { waveChart.addEvent3((name), (msg)); } }
 #else
#define addEngineSnifferEvent(n, msg) {}
#endif /* EFI_ENGINE_SNIFFER */

#if EFI_ENGINE_SNIFFER

#include "eficonsole.h"
#include "status_loop.h"

#define CHART_DELIMETER	'!'
extern WaveChart waveChart;

static char shaft_signal_msg_index[15];

/**
 * This is the number of events in the digital chart which would be displayed
 * on the 'digital sniffer' pane
 */
#if EFI_PROD_CODE
#define WAVE_LOGGING_SIZE 5000
#else
#define WAVE_LOGGING_SIZE 35000
#endif

static char WAVE_LOGGING_BUFFER[WAVE_LOGGING_SIZE];

int waveChartUsedSize;

//#define DEBUG_WAVE 1

/**
 * We want to skip some engine cycles to skip what was scheduled before parameters were changed
 */
static uint32_t skipUntilEngineCycle = 0;

#if ! EFI_UNIT_TEST
extern WaveChart waveChart;
static void resetNow() {
	skipUntilEngineCycle = getRevolutionCounter() + 3;
	waveChart.reset();
}
#endif // EFI_UNIT_TEST

WaveChart::WaveChart() : logging("wave chart", WAVE_LOGGING_BUFFER, sizeof(WAVE_LOGGING_BUFFER)) {
}

void WaveChart::init() {
	isInitialized = true;
	reset();
}

void WaveChart::reset() {
#if DEBUG_WAVE
	efiPrintf("reset while at ", counter);
#endif /* DEBUG_WAVE */
	logging.reset();
	counter = 0;
	startTimeNt = 0;
	collectingData = false;
	logging.appendPrintf( "%s%s", PROTOCOL_ENGINE_SNIFFER, LOG_DELIMITER);
}

void WaveChart::startDataCollection() {
	collectingData = true;
}

bool WaveChart::isStartedTooLongAgo() const {
	/**
	 * Say at 300rpm we should get at least four events per revolution.
	 * That's 300/60*4=20 events per second
	 * engineChartSize/20 is the longest meaningful chart.
	 *
	 */
	efidur_t chartDurationNt = getTimeNowNt() - startTimeNt;
	return startTimeNt.count() != 0 && NT2US(chartDurationNt) > engineConfiguration->engineChartSize * 1000000 / 20;
}

bool WaveChart::isFull() const {
	return counter >= engineConfiguration->engineChartSize;
}

int WaveChart::getSize() {
	return counter;
}

#if ! EFI_UNIT_TEST
static void printStatus() {
	efiPrintf("engine sniffer: %s", boolToString(getTriggerCentral()->isEngineSnifferEnabled));
	efiPrintf("engine sniffer size=%d", engineConfiguration->engineChartSize);
}

void setChartSize(int newSize) {
	if (newSize < 5) {
		return;
	}
	engineConfiguration->engineChartSize = newSize;
	printStatus();
}
#endif // EFI_UNIT_TEST

void WaveChart::publishIfFull() {
	if (isFull() || isStartedTooLongAgo()) {
		publish();
		reset();
	}
}

void WaveChart::publish() {
#if EFI_ENGINE_SNIFFER
	logging.appendPrintf( LOG_DELIMITER);
	waveChartUsedSize = logging.loggingSize();
#if DEBUG_WAVE
	Logging *l = &chart->logging;
	efiPrintf("IT'S TIME", strlen(l->buffer));
#endif // DEBUG_WAVE
	if (getTriggerCentral()->isEngineSnifferEnabled) {
		scheduleLogging(&logging);
	}
#endif /* EFI_ENGINE_SNIFFER */
}

/**
 * @brief	Register an event for digital sniffer
 */
void WaveChart::addEvent3(const char *name, const char * msg) {
#if EFI_TEXT_LOGGING
	ScopePerf perf(PE::EngineSniffer);
	efitick_t nowNt = getTimeNowNt();

	if (nowNt < pauseEngineSnifferUntilNt) {
		return;
	}
	if (!getTriggerCentral()->isEngineSnifferEnabled) {
		return;
	}
	if (skipUntilEngineCycle != 0 && getRevolutionCounter() < skipUntilEngineCycle)
		return;
#if EFI_SIMULATOR
	// todo: add UI control to enable this for firmware if desired
	// engineConfiguration->alignEngineSnifferAtTDC &&
	if (!collectingData) {
		return;
	}
#endif
	efiAssertVoid(ObdCode::CUSTOM_ERR_6651, name!=NULL, "WC: NULL name");

#if EFI_PROD_CODE
	efiAssertVoid(ObdCode::CUSTOM_ERR_6652, getCurrentRemainingStack() > 32, "lowstck#2c");
#endif /* EFI_PROD_CODE */

	efiAssertVoid(ObdCode::CUSTOM_ERR_6653, isInitialized, "chart not initialized");
#if DEBUG_WAVE
	efiPrintf("current", chart->counter);
#endif /* DEBUG_WAVE */
	if (isFull()) {
		return;
	}

	// we have multiple threads writing to the same output buffer
	chibios_rt::CriticalSectionLocker csl;

	if (counter == 0) {
		startTimeNt = nowNt;
	}
	counter++;

	/**
	 * We want smaller times within a chart in order to reduce packet size.
	 */
	/**
	 * todo: migrate to binary fractions in order to eliminate
	 * this division? I do not like division
	 *
	 * at least that's 32 bit division now
	 */
	uint32_t diffUs = NT2US(nowNt - startTimeNt);
	uint32_t time100 = diffUs / ENGINE_SNIFFER_UNIT_US;

	if (logging.remainingSize() > 35) {
		/**
		 * printf is a heavy method, append is used here as a performance optimization
		 */
		logging.appendFast(name);
		logging.appendChar(CHART_DELIMETER);
		logging.appendFast(msg);
		logging.appendChar(CHART_DELIMETER);
//		time100 -= startTime100;

		itoa10(timeBuffer, time100);
		logging.appendFast(timeBuffer);
		logging.appendChar(CHART_DELIMETER);
		logging.terminate();
	}
#endif /* EFI_TEXT_LOGGING */
}

void initWaveChart(WaveChart *chart) {
	strcpy((char*) shaft_signal_msg_index, "x_");
	/**
	 * constructor does not work because we need specific initialization order
	 */
	chart->init();

#if ! EFI_UNIT_TEST
	printStatus();
	addConsoleActionI("chartsize", setChartSize);
	// this is used by HW CI
	addConsoleAction(CMD_RESET_ENGINE_SNIFFER, resetNow);
#endif // EFI_UNIT_TEST
}

void addEngineSnifferOutputPinEvent(NamedOutputPin *pin, bool isRise) {
	if (!engineConfiguration->engineSnifferFocusOnInputs) {
		addEngineSnifferEvent(pin->getShortName(), isRise ? PROTOCOL_ES_UP : PROTOCOL_ES_DOWN);
	}
}

void addEngineSnifferTdcEvent(int rpm) {
	static char rpmBuffer[_MAX_FILLER];
	itoa10(rpmBuffer, rpm);

	waveChart.startDataCollection();

	addEngineSnifferEvent(TOP_DEAD_CENTER_MESSAGE, (char* ) rpmBuffer);
}

void addEngineSnifferLogicAnalyzerEvent(int laIndex, bool isRise) {
	extern const char *laNames[];
	const char *name = laNames[laIndex];

	addEngineSnifferEvent(name, isRise ? PROTOCOL_ES_UP : PROTOCOL_ES_DOWN);
}

void addEngineSnifferCrankEvent(int wheelIndex, int triggerEventIndex, bool isRise) {
	static const char *crankName[2] = { PROTOCOL_CRANK1, PROTOCOL_CRANK2 };

	shaft_signal_msg_index[0] = (isRise ? PROTOCOL_ES_UP : PROTOCOL_ES_DOWN)[0];
	// shaft_signal_msg_index[1] is assigned once and forever in the init method below
	itoa10(&shaft_signal_msg_index[2], triggerEventIndex);

	addEngineSnifferEvent(crankName[wheelIndex], (char* ) shaft_signal_msg_index);
}

void addEngineSnifferVvtEvent(int vvtIndex, bool isRise) {
	extern const char *vvtNames[];
	const char *vvtName = vvtNames[vvtIndex];

	addEngineSnifferEvent(vvtName, isRise ? PROTOCOL_ES_UP : PROTOCOL_ES_DOWN);
}

#endif /* EFI_ENGINE_SNIFFER */

