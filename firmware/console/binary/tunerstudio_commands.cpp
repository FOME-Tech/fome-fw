#include "pch.h"

#include "tunerstudio_impl.h"
#include "tunerstudio.h"
#include "tunerstudio_io.h"

#include "live_data.h"

#include "status_loop.h"

#if EFI_TUNER_STUDIO

void sendErrorCode(TsChannelBase *tsChannel, uint8_t code);

static constexpr size_t getTunerStudioPageSize() {
	return TOTAL_CONFIG_SIZE;
}

// Validate whether the specified offset and count would cause an overrun in the tune.
// Returns true if an overrun would occur.
bool validateOffsetCount(size_t offset, size_t count, TsChannelBase* tsChannel) {
	auto pageSize = getTunerStudioPageSize();
	if (offset + count > pageSize) {
		efiPrintf("TS: Project mismatch? Too much configuration requested offset %d count %d but total size is %d", offset, count, pageSize);
		tunerStudioError(tsChannel, "ERROR: out of range");
		sendErrorCode(tsChannel, TS_RESPONSE_OUT_OF_RANGE);
		return true;
	}

	return false;
}


// This is used to prevent TS from reading/writing when we have just applied a preset, to prevent TS getting confused.
// At the same time an ECU reboot is forced by triggering a fatal error, informing the user to please restart
// the ECU.  Forcing a reboot will force TS to re-read the tune CRC,
bool rebootForPresetPending = false;

static Timer requestPeriodTimer;

/**
 * @brief 'Output' command sends out a snapshot of current values
 * Gauges refresh
 */
void TunerStudio::cmdOutputChannels(TsChannelBase* tsChannel, uint16_t offset, uint16_t count) {
	// Assert that the entire output channels block will fit in a single TS transaction
	static_assert(BLOCKING_FACTOR >= TS_TOTAL_OUTPUT_SIZE + 10);

	if (offset + count > TS_TOTAL_OUTPUT_SIZE) {
		efiPrintf("TS: Version Mismatch? Too much outputs requested %d/%d/%d", offset, count,
				sizeof(TunerStudioOutputChannels));
		sendErrorCode(tsChannel, TS_RESPONSE_OUT_OF_RANGE);
		return;
	}

	engine->outputChannels.outputRequestPeriod
		= 1e6 * requestPeriodTimer.getElapsedSecondsAndReset(getTimeNowNt());

	tsState.outputChannelsCommandCounter++;
	updateTunerStudioState();

	// this method is invoked too often to print any debug information
	uint8_t * scratchBuffer = (uint8_t *)tsChannel->scratchBuffer;
	/**
	 * collect data from all models
	 */
	copyRange(scratchBuffer, getLiveDataFragments(), offset, count);

	tsChannel->writeCrcPacketLocked(TS_RESPONSE_OK, scratchBuffer, count);
}

#endif // EFI_TUNER_STUDIO
