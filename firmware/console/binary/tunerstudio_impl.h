/**
 * @file tunerstudio_impl.h
 */

#pragma once

#include <cstdint>

class TsChannelBase;

typedef enum {
	TS_PLAIN = 0,
	TS_CRC = 1
} ts_response_format_e;

class TunerStudioBase {
public:

protected:
	virtual void cmdOutputChannels(TsChannelBase* tsChannel, uint16_t offset, uint16_t count) = 0;
};

class TunerStudio : public TunerStudioBase {
public:
	int handleCrcCommand(TsChannelBase* tsChannel, uint8_t* data, int incomingPacketSize);
	bool handlePlainCommand(TsChannelBase* tsChannel, uint8_t command);

	void cmdOutputChannels(TsChannelBase* tsChannel, uint16_t offset, uint16_t count) override;
	/**
	 * this command is part of protocol initialization
	 */
	void handleQueryCommand(TsChannelBase* tsChannel, ts_response_format_e mode);
	void handleExecuteCommand(TsChannelBase* tsChannel, char *data, int incomingPacketSize);
	void handleWriteChunkCommand(TsChannelBase* tsChannel, uint16_t offset, uint16_t count,
			void *content);
	void handleCrc32Check(TsChannelBase *tsChannel, uint16_t offset, uint16_t count);
	void handleWriteValueCommand(TsChannelBase* tsChannel, uint16_t offset, uint8_t value);
	void handlePageReadCommand(TsChannelBase* tsChannel, uint16_t offset, uint16_t count);

private:
	void sendErrorCode(TsChannelBase* tsChannel, uint8_t code);
};
