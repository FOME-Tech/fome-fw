#include "g0_analog.h"
#if EFI_PROD_CODE
#include "ports/mpu_util.h"

namespace {

static constexpr size_t G070_ANALOG_CHANNEL_COUNT = 12;
static constexpr size_t G070_DIGITAL_INPUT_COUNT = 4;
static constexpr size_t G070_OUTPUT_COUNT = 4;
static constexpr uint8_t spiAppFrameSize = 36U;
static constexpr uint8_t spiCmdReadAnalog = 0x10U;
static constexpr uint8_t spiCmdReadDigitalAll = 0x12U;
static constexpr uint8_t spiCmdNop = 0x00U;
static constexpr uint8_t spiCmdSetOutput = 0x20U;
static constexpr uint8_t spiCmdDisableOutput = 0x21U;
static constexpr uint8_t spiCmdSetInputMode = 0x30U;
static constexpr uint8_t spiAppHeaderSize = 4U;
static constexpr uint8_t spiResultOk = 0x00U;
static constexpr uint8_t spiResultInvalid = 0x01U;
static constexpr uint8_t spiResultRange = 0x02U;
static constexpr uint8_t spiResultBusy = 0x03U;
static constexpr uint8_t spiDigitalMode = 0x00U;
static constexpr uint16_t G070_OUTPUT_DUTY_MAX = 10000U;
static constexpr spi_device_e G0_SPI_DEVICE = SPI_DEVICE_5;
static constexpr brain_pin_e G0_SPI_CS_PIN = Gpio::F6;
static constexpr size_t G0_FIRST_ADC_INDEX = EFI_ADC_20 - EFI_ADC_0;
// Edit this value to tune the fixed G0 ADC poll rate. 10 ms = 100 Hz.
static constexpr uint32_t G0_POLL_INTERVAL_MS = 10;

static SPIConfig g0AnalogSpiConfig = {
		.circular = false,
		.end_cb = NULL,
		.ssport = NULL,
		.sspad = 0,
		.cfg1 = 7 | SPI_CFG1_MBR_2 | SPI_CFG1_MBR_1 | SPI_CFG1_MBR_0,
		.cfg2 = 0};

static NO_CACHE uint8_t txBuf[spiAppFrameSize];
static NO_CACHE uint8_t rxBuf[spiAppFrameSize];

struct G070OutputState {
	bool enabled = false;
	uint32_t frequencyHz = 0;
	uint16_t duty = 0;
	bool dirty = false;
	bool pendingAck = false;
	bool sentEnabled = false;
	uint32_t sentFrequencyHz = 0;
	uint16_t sentDuty = 0;
};

enum class G070RequestType : uint8_t {
	None,
	SetOutput,
	DisableOutput,
};

struct G070PendingRequest {
	G070RequestType type = G070RequestType::None;
	size_t outputIndex = 0;
};

class G070SpiAdcProvider final : public AdcProvider {
public:
	const char* name() const override {
		return "G070SpiAdc";
	}

	SPIDriver* spiDevice = nullptr;

	bool enable(const char* name, size_t idx) override {
		(void)name;

		if (idx >= G070_ANALOG_CHANNEL_COUNT) {
			return false;
		}

		m_enabled[idx] = true;
		return true;
	}

	void disable(size_t idx) override {
		if (idx >= G070_ANALOG_CHANNEL_COUNT) {
			return;
		}

		m_enabled[idx] = false;
	}

	float get(size_t idx) const override {
		if (idx >= G070_ANALOG_CHANNEL_COUNT) {
			return 0.0f;
		}

		if (!m_ready) {
			return 0.0f;
		}

		return m_millivolts[idx] * 0.001f;
	}

	bool readDigitalInput(size_t idx) const {
		if (idx >= G070_DIGITAL_INPUT_COUNT) {
			return false;
		}

		if (!m_digitalReady) {
			return false;
		}

		return m_digitalLevels[idx];
	}

	void setLowsideOutput(size_t idx, bool value) {
		requestOutput(idx, value, 0, value ? G070_OUTPUT_DUTY_MAX : 0);
	}

	void disableLowsideOutput(size_t idx) {
		requestOutput(idx, false, 0, 0);
	}

	void setLowsidePwm(size_t idx, uint32_t frequencyHz, uint16_t duty) {
		requestOutput(idx, true, frequencyHz, duty);
	}

	void start() {
		if (m_started) {
			efiPrintf("G070 SPI ADC provider: already started");
			return;
		}

		m_started = true;

		turnOnSpi(G0_SPI_DEVICE);

		spiDevice = getSpiDevice(G0_SPI_DEVICE);

		initSpiCs(&g0AnalogSpiConfig, G0_SPI_CS_PIN);
		palSetPad(g0AnalogSpiConfig.ssport, g0AnalogSpiConfig.sspad);

		spiStart(spiDevice, &g0AnalogSpiConfig);

		chThdCreateStatic(m_threadWa, sizeof(m_threadWa), NORMALPRIO, threadEntry, this);
	}

private:
	bool m_started = false;

	static THD_FUNCTION(threadEntry, arg) {
		static_cast<G070SpiAdcProvider*>(arg)->thread();
	}

	void thread() {
		chRegSetThreadName("G070 ADC");

		while (true) {
			pollIo();

			chThdSleepMilliseconds(G0_POLL_INTERVAL_MS);
		}
	}

	void pollIo() {
		G070PendingRequest nextRequest;

		spiAcquireBus(spiDevice);

		clearFrame(txBuf);
		clearFrame(rxBuf);

		prepareRequest(txBuf, nextRequest);

		spiSelect(spiDevice);
		spiExchange(spiDevice, spiAppFrameSize, txBuf, rxBuf);
		spiUnselect(spiDevice);

		spiReleaseBus(spiDevice);

		parseResponse(rxBuf);
		m_pendingRequest = nextRequest;
	}

	void prepareRequest(uint8_t* tx, G070PendingRequest& nextRequest) {
		nextRequest = {};

		// The G0 SPI application protocol is pipelined: the response we receive now
		// belongs to the previous request, while this request schedules the next reply.
		if (m_nextDigitalInputToConfigure <= G070_DIGITAL_INPUT_COUNT) {
			tx[0] = spiCmdSetInputMode;
			tx[1] = static_cast<uint8_t>(m_nextDigitalInputToConfigure);
			tx[2] = spiDigitalMode;
			m_nextDigitalInputToConfigure++;
			return;
		}

		for (size_t i = 0; i < G070_OUTPUT_COUNT; i++) {
			if (!m_outputs[i].dirty || m_outputs[i].pendingAck) {
				continue;
			}

			const auto& output = m_outputs[i];
			nextRequest.outputIndex = i;

			if (output.enabled) {
				tx[0] = spiCmdSetOutput;
				tx[1] = static_cast<uint8_t>(i + 1);
				putU32(tx, 2, output.frequencyHz);
				putU16(tx, 6, output.duty);
				nextRequest.type = G070RequestType::SetOutput;
			} else {
				tx[0] = spiCmdDisableOutput;
				tx[1] = static_cast<uint8_t>(i + 1);
				nextRequest.type = G070RequestType::DisableOutput;
			}

			m_outputs[i].pendingAck = true;
			m_outputs[i].sentEnabled = output.enabled;
			m_outputs[i].sentFrequencyHz = output.frequencyHz;
			m_outputs[i].sentDuty = output.duty;
			return;
		}

		if (m_pollAnalogNext) {
			tx[0] = spiCmdReadAnalog;
		} else {
			tx[0] = spiCmdReadDigitalAll;
		}

		m_pollAnalogNext = !m_pollAnalogNext;
	}

	void parseResponse(const uint8_t* rx) {
		if (m_pendingRequest.type == G070RequestType::SetOutput ||
			m_pendingRequest.type == G070RequestType::DisableOutput) {
			parseOutputAck(rx);
		}

		switch (rx[2]) {
			case spiCmdReadAnalog:
				parseAnalogResponse(rx);
				break;
			case spiCmdReadDigitalAll:
				parseDigitalResponse(rx);
				break;
			default:
				break;
		}
	}

	void parseOutputAck(const uint8_t* rx) {
		if (m_pendingRequest.outputIndex >= G070_OUTPUT_COUNT) {
			return;
		}

		auto& output = m_outputs[m_pendingRequest.outputIndex];
		output.pendingAck = false;

		if (rx[1] != spiResultOk) {
			output.dirty = true;
			return;
		}

		output.dirty = output.enabled != output.sentEnabled || output.frequencyHz != output.sentFrequencyHz ||
					   output.duty != output.sentDuty;
	}

	void parseAnalogResponse(const uint8_t* rx) {
		const uint8_t status = rx[0];
		const uint8_t result = rx[1];
		const uint8_t payloadLength = rx[3];

		// static constexpr size_t printThrottle = 50;
		// static size_t printCounter = 0;

		// const bool shouldPrint = (++printCounter >= printThrottle);

		/*
		if (shouldPrint) {
			printCounter = 0;

			efiPrintf(
					"G070 ADC response: status=0x%02X result=0x%02X command=0x%02X payloadLength=%d",
					status,
					result,
					command,
					payloadLength);
		}
		*/

		if (status != 0x00 && status != 0x01) {
			m_ready = false;
			return;
		}

		if (result != spiResultOk) {
			m_ready = false;
			return;
		}

		if (payloadLength != 26) {
			m_ready = false;
			return;
		}

		const bool analogReady = rx[spiAppHeaderSize] != 0;
		const uint8_t channelCount = rx[spiAppHeaderSize + 1];

		if (!analogReady) {
			m_ready = false;
			return;
		}

		const size_t count = channelCount < G070_ANALOG_CHANNEL_COUNT ? channelCount : G070_ANALOG_CHANNEL_COUNT;

		for (size_t i = 0; i < count; i++) {
			const uint8_t offset = static_cast<uint8_t>(spiAppHeaderSize + 2 + i * 2);

			m_millivolts[i] = getU16(rx, offset);
		}

		/*
		if (shouldPrint) {
			efiPrintf("G070 ADC values (mV):");

			for (size_t i = 0; i < count; i++) {
				efiPrintf(" %d", m_millivolts[i]);
			}

			efiPrintf("");
		}
		*/

		m_ready = true;
	}

	void parseDigitalResponse(const uint8_t* rx) {
		const uint8_t status = rx[0];
		const uint8_t result = rx[1];
		const uint8_t payloadLength = rx[3];

		if (status != 0x00 && status != 0x01) {
			m_digitalReady = false;
			return;
		}

		if (result != spiResultOk) {
			m_digitalReady = false;
			return;
		}

		if (payloadLength != 25) {
			m_digitalReady = false;
			return;
		}

		const uint8_t inputCount = rx[spiAppHeaderSize];

		if (inputCount != G070_DIGITAL_INPUT_COUNT) {
			m_digitalReady = false;
			return;
		}

		for (size_t i = 0; i < G070_DIGITAL_INPUT_COUNT; i++) {
			const uint8_t offset = static_cast<uint8_t>(spiAppHeaderSize + 1 + i * 6);
			const uint8_t flags = rx[offset + 1];

			m_digitalLevels[i] = (flags & 0x01U) != 0;
		}

		m_digitalReady = true;
	}

	static void clearFrame(uint8_t* frame) {
		for (size_t i = 0; i < spiAppFrameSize; i++) {
			frame[i] = 0;
		}
	}

	static void putU16(uint8_t* buffer, uint8_t offset, uint16_t value) {
		buffer[offset] = static_cast<uint8_t>(value);
		buffer[offset + 1] = static_cast<uint8_t>(value >> 8);
	}

	static void putU32(uint8_t* buffer, uint8_t offset, uint32_t value) {
		buffer[offset] = static_cast<uint8_t>(value);
		buffer[offset + 1] = static_cast<uint8_t>(value >> 8);
		buffer[offset + 2] = static_cast<uint8_t>(value >> 16);
		buffer[offset + 3] = static_cast<uint8_t>(value >> 24);
	}

	static uint16_t getU16(const uint8_t* buffer, uint8_t offset) {
		return static_cast<uint16_t>(buffer[offset]) |
			   static_cast<uint16_t>(static_cast<uint16_t>(buffer[offset + 1]) << 8);
	}

	void requestOutput(size_t idx, bool enabled, uint32_t frequencyHz, uint16_t duty) {
		if (idx >= G070_OUTPUT_COUNT) {
			return;
		}

		if (duty > G070_OUTPUT_DUTY_MAX) {
			duty = G070_OUTPUT_DUTY_MAX;
		}

		auto& output = m_outputs[idx];
		const bool changed = output.enabled != enabled || output.frequencyHz != frequencyHz || output.duty != duty;

		output.enabled = enabled;
		output.frequencyHz = frequencyHz;
		output.duty = duty;
		if (changed) {
			output.dirty = true;
		}
	}

private:
	bool m_enabled[G070_ANALOG_CHANNEL_COUNT] = {};
	volatile bool m_ready = false;
	volatile uint16_t m_millivolts[G070_ANALOG_CHANNEL_COUNT] = {};
	volatile bool m_digitalReady = false;
	volatile bool m_digitalLevels[G070_DIGITAL_INPUT_COUNT] = {};
	G070OutputState m_outputs[G070_OUTPUT_COUNT] = {};
	G070PendingRequest m_pendingRequest = {};
	size_t m_nextDigitalInputToConfigure = 1;
	bool m_pollAnalogNext = true;

	THD_WORKING_AREA(m_threadWa, 512);
};

} // namespace

namespace {

static G070SpiAdcProvider g070SpiAdcProvider;

#if EFI_PROD_CODE
class G070LowsideHardwarePwm final : public hardware_pwm {
public:
	explicit G070LowsideHardwarePwm(size_t outputIndex)
		: m_outputIndex(outputIndex) {}

	void start(float frequencyHz, float duty) {
		m_frequencyHz = frequencyHz < 1 ? 1 : static_cast<uint32_t>(frequencyHz);
		setDuty(duty);
	}

	void setDuty(float duty) override {
		if (duty < 0) {
			duty = 0;
		} else if (duty > 1) {
			duty = 1;
		}

		const auto scaledDuty = static_cast<uint16_t>(duty * G070_OUTPUT_DUTY_MAX);
		g070SpiAdcProvider.setLowsidePwm(m_outputIndex, m_frequencyHz, scaledDuty);
	}

private:
	const size_t m_outputIndex;
	uint32_t m_frequencyHz = 1;
};

static G070LowsideHardwarePwm g070LowsidePwms[G070_OUTPUT_COUNT] = {
		G070LowsideHardwarePwm(0),
		G070LowsideHardwarePwm(1),
		G070LowsideHardwarePwm(2),
		G070LowsideHardwarePwm(3),
};
#endif

} // namespace

void startG070SpiAdcProvider() {
	static bool started = false;

	if (started) {
		efiPrintf("startG070SpiAdcProvider: already started");
		return;
	}

	started = true;

	g070SpiAdcProvider.start();

	registerAdcProvider(g070SpiAdcProvider, G0_FIRST_ADC_INDEX, G070_ANALOG_CHANNEL_COUNT);
}

bool readG070DigitalInput(size_t idx) {
	return g070SpiAdcProvider.readDigitalInput(idx);
}

bool isG070LowsidePin(brain_pin_e pin) {
	return pin >= Gpio::G0_LOWSIDE_0 && pin <= Gpio::G0_LOWSIDE_3;
}

void setG070LowsideOutput(brain_pin_e pin, bool value) {
	if (!isG070LowsidePin(pin)) {
		return;
	}

	g070SpiAdcProvider.setLowsideOutput(pin - Gpio::G0_LOWSIDE_0, value);
}

void disableG070LowsideOutput(brain_pin_e pin) {
	if (!isG070LowsidePin(pin)) {
		return;
	}

	g070SpiAdcProvider.disableLowsideOutput(pin - Gpio::G0_LOWSIDE_0);
}

hardware_pwm* tryInitG070LowsidePwm(brain_pin_e pin, float frequencyHz, float duty) {
	if (!isG070LowsidePin(pin)) {
		return nullptr;
	}

	auto& pwm = g070LowsidePwms[pin - Gpio::G0_LOWSIDE_0];
	pwm.start(frequencyHz, duty);
	return &pwm;
}

#else

void startG070SpiAdcProvider() {}

bool readG070DigitalInput(size_t idx) {
	(void)idx;
	return false;
}

bool isG070LowsidePin(brain_pin_e pin) {
	return pin >= Gpio::G0_LOWSIDE_0 && pin <= Gpio::G0_LOWSIDE_3;
}

void setG070LowsideOutput(brain_pin_e pin, bool value) {
	(void)pin;
	(void)value;
}

void disableG070LowsideOutput(brain_pin_e pin) {
	(void)pin;
}

hardware_pwm* tryInitG070LowsidePwm(brain_pin_e pin, float frequencyHz, float duty) {
	(void)pin;
	(void)frequencyHz;
	(void)duty;
	return nullptr;
}

#endif
