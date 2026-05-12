#pragma once

#include "g0_extension_io.h"
#include "../../ext/g0_firmware/for_fome/g0_spi_protocol.h"

#if EFI_PROD_CODE

namespace g0_extension {

namespace protocol = ::g0_spi_protocol;

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
	OutputState m_outputs[protocol::outputCount] = {};
};

AdcProvider& adcProvider();
void startProvider();
bool readDigitalInput(size_t idx);
void setLowsideOutput(size_t idx, bool value);
void disableLowsideOutput(size_t idx);
void setLowsidePwm(size_t idx, uint32_t frequencyHz, uint16_t duty);

} // namespace g0_extension

#endif
