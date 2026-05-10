#pragma once

#include "g0_extension_io.h"

#if EFI_PROD_CODE

namespace g0_extension {

static constexpr size_t AnalogChannelCount = 12;
static constexpr size_t DigitalInputCount = 4;
static constexpr size_t OutputCount = 4;
static constexpr size_t SpiAppFrameSize = 36U;
static constexpr uint8_t SpiCmdReadAnalog = 0x10U;
static constexpr uint8_t SpiCmdReadDigitalAll = 0x12U;
static constexpr uint8_t SpiCmdSetOutput = 0x20U;
static constexpr uint8_t SpiCmdDisableOutput = 0x21U;
static constexpr uint8_t SpiCmdSetInputMode = 0x30U;
static constexpr uint8_t SpiAppHeaderSize = 4U;
static constexpr uint8_t SpiResultOk = 0x00U;
static constexpr uint8_t SpiDigitalMode = 0x00U;
static constexpr uint16_t OutputDutyMax = 10000U;
static constexpr spi_device_e SpiDevice = SPI_DEVICE_5;
static constexpr brain_pin_e SpiCsPin = Gpio::F6;
static constexpr size_t FirstAdcIndex = EFI_ADC_20 - EFI_ADC_0;
static constexpr uint32_t PollIntervalMs = 10;

struct OutputState {
	bool enabled = false;
	uint32_t frequencyHz = 0;
	uint16_t duty = 0;
	bool dirty = false;
	bool pendingAck = false;
	bool sentEnabled = false;
	uint32_t sentFrequencyHz = 0;
	uint16_t sentDuty = 0;
};

enum class RequestType : uint8_t {
	None,
	SetOutput,
	DisableOutput,
};

struct PendingRequest {
	RequestType type = RequestType::None;
	size_t outputIndex = 0;
};

class OutputManager {
public:
	void prepareRequest(uint8_t* tx, PendingRequest& nextRequest);
	void parseAck(const PendingRequest& pendingRequest, const uint8_t* rx);
	void requestOutput(size_t idx, bool enabled, uint32_t frequencyHz, uint16_t duty);

private:
	OutputState m_outputs[OutputCount] = {};
};

AdcProvider& adcProvider();
void startProvider();
bool readDigitalInput(size_t idx);
void setLowsideOutput(size_t idx, bool value);
void disableLowsideOutput(size_t idx);
void setLowsidePwm(size_t idx, uint32_t frequencyHz, uint16_t duty);

} // namespace g0_extension

#endif
