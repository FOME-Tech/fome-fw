#include "g0_extension_io_impl.h"

#if EFI_PROD_CODE

#include "ports/mpu_util.h"

namespace g0_extension {
namespace {

static SPIConfig g0ExtensionSpiConfig = {
		.circular = false,
		.end_cb = NULL,
		.ssport = NULL,
		.sspad = 0,
		.cfg1 = 7 | SPI_CFG1_MBR_2 | SPI_CFG1_MBR_1 | SPI_CFG1_MBR_0,
		.cfg2 = 0};

static NO_CACHE protocol::AppFrame txBuf;
static NO_CACHE protocol::AppFrame rxBuf;

static void clearFrame(protocol::AppFrame& frame) {
	for (size_t i = 0; i < protocol::appFrameSize; i++) {
		frame.bytes[i] = 0;
	}
}

class G0ExtensionSpiAdcProvider final : public AdcProvider {
public:
	const char* name() const override {
		return "G0ExtensionSpiAdc";
	}

	bool enable(const char* name, size_t idx) override {
		(void)name;

		if (idx >= protocol::analogChannelCount) {
			return false;
		}

		m_enabled[idx] = true;
		return true;
	}

	void disable(size_t idx) override {
		if (idx >= protocol::analogChannelCount) {
			return;
		}

		m_enabled[idx] = false;
	}

	float get(size_t idx) const override {
		if (idx >= protocol::analogChannelCount || !m_ready) {
			return 0.0f;
		}

		return m_millivolts[idx] * 0.001f;
	}

	bool readDigitalInput(size_t idx) const {
		if (idx >= protocol::digitalInputCount || !m_digitalReady) {
			return false;
		}

		return m_digitalLevels[idx];
	}

	void setLowsideOutput(size_t idx, bool value) {
		m_outputs.requestOutput(idx, value, 0, value ? protocol::outputDutyMax : 0);
	}

	void disableLowsideOutput(size_t idx) {
		m_outputs.requestOutput(idx, false, 0, 0);
	}

	void setLowsidePwm(size_t idx, uint32_t frequencyHz, uint16_t duty) {
		m_outputs.requestOutput(idx, true, frequencyHz, duty);
	}

	void start() {
		if (m_started) {
			efiPrintf("G0 extension I/O: already started");
			return;
		}

		m_started = true;

		turnOnSpi(SpiDevice);
		m_spiDevice = getSpiDevice(SpiDevice);

		initSpiCs(&g0ExtensionSpiConfig, SpiCsPin);
		palSetPad(g0ExtensionSpiConfig.ssport, g0ExtensionSpiConfig.sspad);
		spiStart(m_spiDevice, &g0ExtensionSpiConfig);

		chThdCreateStatic(m_threadWa, sizeof(m_threadWa), NORMALPRIO, threadEntry, this);
	}

private:
	static THD_FUNCTION(threadEntry, arg) {
		static_cast<G0ExtensionSpiAdcProvider*>(arg)->thread();
	}

	void thread() {
		chRegSetThreadName("G0 Ext IO");

		while (true) {
			pollIo();
			chThdSleepMilliseconds(PollIntervalMs);
		}
	}

	void pollIo() {
		PendingRequest nextRequest;

		spiAcquireBus(m_spiDevice);
		clearFrame(txBuf);
		clearFrame(rxBuf);
		prepareRequest(txBuf, nextRequest);

		spiSelect(m_spiDevice);
		spiExchange(m_spiDevice, protocol::appFrameSize, txBuf.bytes, rxBuf.bytes);
		spiUnselect(m_spiDevice);
		spiReleaseBus(m_spiDevice);

		parseResponse(rxBuf);
		m_pendingRequest = nextRequest;
	}

	void prepareRequest(protocol::AppFrame& tx, PendingRequest& nextRequest) {
		nextRequest = {};

		// The G0 app protocol is pipelined: the response we receive now
		// belongs to the previous request, while this request schedules the next reply.
		if (m_nextDigitalInputToConfigure <= protocol::digitalInputCount) {
			tx.setInputModeRequest.command = protocol::cmdSetInputMode;
			tx.setInputModeRequest.input = static_cast<uint8_t>(m_nextDigitalInputToConfigure);
			tx.setInputModeRequest.mode = protocol::digitalMode;
			m_nextDigitalInputToConfigure++;
			return;
		}

		m_outputs.prepareRequest(tx, nextRequest);
		if (nextRequest.type != RequestType::None) {
			return;
		}

		tx.commandRequest.command = m_pollAnalogNext ? protocol::cmdReadAnalog : protocol::cmdReadDigitalAll;
		m_pollAnalogNext = !m_pollAnalogNext;
	}

	void parseResponse(const protocol::AppFrame& rx) {
		if (m_pendingRequest.type == RequestType::SetOutput ||
			m_pendingRequest.type == RequestType::DisableOutput) {
			m_outputs.parseAck(m_pendingRequest, rx);
		}

		switch (rx.responseHeader.command) {
			case protocol::cmdReadAnalog:
				parseAnalogResponse(rx);
				break;
			case protocol::cmdReadDigitalAll:
				parseDigitalResponse(rx);
				break;
			default:
				break;
		}
	}

	void parseAnalogResponse(const protocol::AppFrame& rx) {
		const auto& response = rx.analogResponse;
		const auto& header = response.header;

		if (header.status != protocol::statusReady && header.status != protocol::statusUpdateMode) {
			m_ready = false;
			return;
		}

		if (header.result != protocol::resultOk || header.payloadLength != protocol::analogPayloadLength) {
			m_ready = false;
			return;
		}

		if (!response.ready) {
			m_ready = false;
			return;
		}

		const size_t count = response.channelCount < protocol::analogChannelCount ? response.channelCount : protocol::analogChannelCount;
		for (size_t i = 0; i < count; i++) {
			m_millivolts[i] = response.millivolts[i];
		}

		m_ready = true;
	}

	void parseDigitalResponse(const protocol::AppFrame& rx) {
		const auto& response = rx.digitalAllResponse;
		const auto& header = response.header;

		if (header.status != protocol::statusReady && header.status != protocol::statusUpdateMode) {
			m_digitalReady = false;
			return;
		}

		if (header.result != protocol::resultOk || header.payloadLength != protocol::digitalAllPayloadLength) {
			m_digitalReady = false;
			return;
		}

		if (response.inputCount != protocol::digitalInputCount) {
			m_digitalReady = false;
			return;
		}

		for (size_t i = 0; i < protocol::digitalInputCount; i++) {
			m_digitalLevels[i] = (response.inputs[i].flags & 0x01U) != 0;
		}

		m_digitalReady = true;
	}

private:
	bool m_started = false;
	SPIDriver* m_spiDevice = nullptr;
	bool m_enabled[protocol::analogChannelCount] = {};
	volatile bool m_ready = false;
	volatile uint16_t m_millivolts[protocol::analogChannelCount] = {};
	volatile bool m_digitalReady = false;
	volatile bool m_digitalLevels[protocol::digitalInputCount] = {};
	OutputManager m_outputs;
	PendingRequest m_pendingRequest = {};
	size_t m_nextDigitalInputToConfigure = 1;
	bool m_pollAnalogNext = true;
	THD_WORKING_AREA(m_threadWa, 512);
};

static G0ExtensionSpiAdcProvider g0ExtensionSpiAdcProvider;

} // namespace

AdcProvider& adcProvider() {
	return g0ExtensionSpiAdcProvider;
}

void startProvider() {
	g0ExtensionSpiAdcProvider.start();
}

bool readDigitalInput(size_t idx) {
	return g0ExtensionSpiAdcProvider.readDigitalInput(idx);
}

void setLowsideOutput(size_t idx, bool value) {
	g0ExtensionSpiAdcProvider.setLowsideOutput(idx, value);
}

void disableLowsideOutput(size_t idx) {
	g0ExtensionSpiAdcProvider.disableLowsideOutput(idx);
}

void setLowsidePwm(size_t idx, uint32_t frequencyHz, uint16_t duty) {
	g0ExtensionSpiAdcProvider.setLowsidePwm(idx, frequencyHz, duty);
}

} // namespace g0_extension

#endif
