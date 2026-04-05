#include "tunerstudio.h"

class SerialTsChannel final : public TsChannelBase {
public:
	SerialTsChannel(SerialDriver& driver)
		: TsChannelBase("Serial")
		, m_driver(&driver) {}

	void start();
	void stop() override;

	void write(const uint8_t* buffer, size_t size, bool isEndOfPacket) override;
	size_t readTimeout(uint8_t* buffer, size_t size, int timeout) override;

private:
	SerialDriver* const m_driver;
};

void SerialTsChannel::start() {
	SerialConfig cfg = {};

	sdStart(m_driver, &cfg);
}

void SerialTsChannel::stop() {
	sdStop(m_driver);
}

void SerialTsChannel::write(const uint8_t* buffer, size_t size, bool /*isEndOfPacket*/) {
	chnWriteTimeout(m_driver, buffer, size, BINARY_IO_TIMEOUT);
}

size_t SerialTsChannel::readTimeout(uint8_t* buffer, size_t size, int timeout) {
	return chnReadTimeout(m_driver, buffer, size, timeout);
}

struct SerialThread : public TunerstudioThread {
	SerialThread(SerialDriver& driver)
		: TunerstudioThread("Primary TS Channel")
		, channel(driver) {}

	TsChannelBase* setupChannel() {
		channel.start();

		return &channel;
	}

	SerialTsChannel channel;
};

static SerialThread primary(SD1);
static SerialThread secondary(SD2);

void startSerialChannels() {
	primary.startThread();
	secondary.startThread();
}
