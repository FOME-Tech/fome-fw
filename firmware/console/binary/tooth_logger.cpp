/*
 * @file tooth_logger.cpp
 *
 * @date Jul 7, 2019
 * @author Matthew Kennedy
 */

#include "pch.h"

#if EFI_TOOTH_LOGGER

/**
 * Engine idles around 20Hz and revs up to 140Hz, at 60/2 and 8 cylinders we have about 20Khz events
 * If we can read buffer at 50Hz we want buffer to be about 400 elements.
 */

static_assert(sizeof(composite_logger_s) == COMPOSITE_PACKET_SIZE, "composite packet size");

static volatile bool ToothLoggerEnabled = false;
static uint32_t lastEdgeTimestamp = 0;

static bool wasSecondary = false;
static bool currentTrigger1 = false;
static bool camStates[4] = {false};
static bool currentTdc = false;

#if EFI_UNIT_TEST
#include "logicdata.h"

static std::vector<CompositeEvent> events;

const std::vector<CompositeEvent>& getCompositeEvents() {
	return events;
}

void SetNextCompositeEntry(efitick_t timestamp) {
	CompositeEvent event;

	event.timestamp = timestamp;
	event.primaryTrigger = currentTrigger1;
	event.secondaryTrigger = camStates[0];
	event.isTDC = currentTdc;
	event.sync = engine->triggerCentral.triggerState.getShaftSynchronized();

	events.push_back(event);
}

void EnableToothLogger() {
	ToothLoggerEnabled = true;
	events.clear();
}

void DisableToothLogger() {
	ToothLoggerEnabled = false;
}

#else // not EFI_UNIT_TEST

static constexpr size_t bufferCount = BIG_BUFFER_SIZE / sizeof(CompositeBuffer);
static_assert(bufferCount >= 2);

static CompositeBuffer* buffers = nullptr;
static chibios_rt::Mailbox<CompositeBuffer*, bufferCount> freeBuffers;
static chibios_rt::Mailbox<CompositeBuffer*, bufferCount> filledBuffers;

static CompositeBuffer* currentBuffer = nullptr;

static void setToothLogReady(bool value) {
#if EFI_TUNER_STUDIO && (EFI_PROD_CODE || EFI_SIMULATOR)
	engine->outputChannels.toothLogReady = value;
#endif // EFI_TUNER_STUDIO
}

static BigBufferHandle bufferHandle;

void EnableToothLogger() {
	chibios_rt::CriticalSectionLocker csl;

	bufferHandle = getBigBuffer(BigBufferUser::ToothLogger);
	if (!bufferHandle) {
		return;
	}

	buffers = bufferHandle.get<CompositeBuffer>();

	// Reset all buffers
	for (size_t i = 0; i < bufferCount; i++) {
		buffers[i].nextIdx = 0;
	}

	// Reset state
	currentBuffer = nullptr;

	// Empty the filled buffer list
	CompositeBuffer* dummy;
	while (MSG_TIMEOUT != filledBuffers.fetchI(&dummy)) ;

	// Put all buffers in the free list
	for (size_t i = 0; i < bufferCount; i++) {
		freeBuffers.postI(&buffers[i]);
	}

	// Reset the last edge to now - this prevents the first edge logged from being bogus
	lastEdgeTimestamp = getTimeNowUs();

	// Enable logging of edges as they come
	ToothLoggerEnabled = true;

	setToothLogReady(false);
}

void DisableToothLogger() {
	chibios_rt::CriticalSectionLocker csl;

	// Release the big buffer for another user
	bufferHandle = {};
	buffers = nullptr;

	ToothLoggerEnabled = false;
	setToothLogReady(false);
}

static CompositeBuffer* GetToothLoggerBufferImpl(sysinterval_t timeout) {
	CompositeBuffer* buffer;
	msg_t msg = filledBuffers.fetch(&buffer, timeout);

	if (msg == MSG_TIMEOUT) {
		setToothLogReady(false);
		return nullptr;
	}

	if (msg != MSG_OK) {
		// What even happened if we didn't get timeout, but also didn't get OK?
		return nullptr;
	}

	return buffer;
}

CompositeBuffer* GetToothLoggerBufferNonblocking() {
	return GetToothLoggerBufferImpl(TIME_IMMEDIATE);
}

CompositeBuffer* GetToothLoggerBufferBlocking() {
	return GetToothLoggerBufferImpl(TIME_INFINITE);
}

void ReturnToothLoggerBuffer(CompositeBuffer* buffer) {
	chibios_rt::CriticalSectionLocker csl;

	msg_t msg = freeBuffers.postI(buffer);
	efiAssertVoid(ObdCode::OBD_PCM_Processor_Fault, msg == MSG_OK, "Composite logger post to free buffer fail");

	// If the used list is empty, clear the ready flag
	if (filledBuffers.getUsedCountI() == 0) {
		setToothLogReady(false);
	}
}

static CompositeBuffer* findBuffer(efitick_t timestamp) {
	CompositeBuffer* buffer;

	if (!currentBuffer) {
		// try and find a buffer, if none available, we can't log
		if (MSG_OK != freeBuffers.fetchI(&buffer)) {
			return nullptr;
		}

		// Record the time of the last buffer swap so we can force a swap after a minimum period of time
		// This ensures the user sees *something* even if they don't have enough trigger events
		// to fill the buffer.
		buffer->startTime.reset(timestamp);
		buffer->nextIdx = 0;

		currentBuffer = buffer;
	}

	return currentBuffer;
}

static void SetNextCompositeEntry(efitick_t timestamp) {
	// This is called from multiple interrupts/threads, so we need a lock.
	chibios_rt::CriticalSectionLocker csl;

	CompositeBuffer* buffer = findBuffer(timestamp);

	if (!buffer) {
		// All buffers are full, nothing to do here.
		return;
	}

	size_t idx = buffer->nextIdx;
	auto nextIdx = idx + 1;
	buffer->nextIdx = nextIdx;

	if (idx < efi::size(buffer->buffer)) {
		composite_logger_s* entry = &buffer->buffer[idx];

		uint32_t nowUs = NT2US(timestamp);

		// TS uses big endian, grumble
		entry->timestamp = SWAP_UINT32(nowUs);
		entry->priLevel = currentTrigger1;
		entry->cam1 = camStates[0];
		entry->cam2 = camStates[1];
		entry->cam3 = camStates[2];
		entry->cam4 = camStates[3];
		entry->trigger = wasSecondary;
		entry->tdc = currentTdc;
		entry->sync = engine->triggerCentral.triggerState.getShaftSynchronized();
	}

	// if the buffer is full...
	bool bufferFull = nextIdx >= efi::size(buffer->buffer);
	// ... or it's been too long since the last flush
	bool bufferTimedOut = buffer->startTime.hasElapsedSec(5);

	// Then cycle buffers and set the ready flag.
	if (bufferFull || bufferTimedOut) {
		// Post to the output queue
		filledBuffers.postI(buffer);

		// Null the current buffer so we get a new one next time
		currentBuffer = nullptr;

		// Flag that we are ready
		setToothLogReady(true);
	}
}

#endif // EFI_UNIT_TEST

static void LogTriggerTooth(efitick_t timestamp) {
	// bail if we aren't enabled
	if (!ToothLoggerEnabled) {
		return;
	}

	// Don't log at significant engine speed
	if (!getTriggerCentral()->isEngineSnifferEnabled) {
		return;
	}

	ScopePerf perf(PE::LogTriggerTooth);

	SetNextCompositeEntry(timestamp);
}

void LogPrimaryTriggerTooth(efitick_t timestamp, bool state) {
	wasSecondary = false;
	currentTrigger1 = state;

	LogTriggerTooth(timestamp);
}

void LogCamTriggerTooth(efitick_t timestamp, int camIndex, bool state) {
	wasSecondary = true;

	camStates[camIndex] = state;

	LogTriggerTooth(timestamp);
}

void LogTriggerTopDeadCenter(efitick_t timestamp) {
	// bail if we aren't enabled
	if (!ToothLoggerEnabled) {
		return;
	}
	currentTdc = true;
	SetNextCompositeEntry(timestamp);
	currentTdc = false;
	SetNextCompositeEntry(timestamp + 10);
}

void EnableToothLoggerIfNotEnabled() {
	if (!ToothLoggerEnabled) {
		EnableToothLogger();
	}
}

#endif /* EFI_TOOTH_LOGGER */
