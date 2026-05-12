#include "g0_extension_io_impl.h"

#if EFI_PROD_CODE

namespace g0_extension {
namespace {

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

} // namespace

void OutputManager::prepareRequest(uint8_t* tx, PendingRequest& nextRequest) {
	nextRequest = {};

	for (size_t i = 0; i < protocol::outputCount; i++) {
		if (!m_outputs[i].dirty || m_outputs[i].pendingAck) {
			continue;
		}

		const auto& output = m_outputs[i];
		nextRequest.outputIndex = i;

		if (output.enabled) {
			tx[0] = protocol::cmdSetOutput;
			tx[1] = static_cast<uint8_t>(i + 1);
			putU32(tx, 2, output.frequencyHz);
			putU16(tx, 6, output.duty);
			nextRequest.type = RequestType::SetOutput;
		} else {
			tx[0] = protocol::cmdDisableOutput;
			tx[1] = static_cast<uint8_t>(i + 1);
			nextRequest.type = RequestType::DisableOutput;
		}

		m_outputs[i].pendingAck = true;
		m_outputs[i].sentEnabled = output.enabled;
		m_outputs[i].sentFrequencyHz = output.frequencyHz;
		m_outputs[i].sentDuty = output.duty;
		return;
	}
}

void OutputManager::parseAck(const PendingRequest& pendingRequest, const uint8_t* rx) {
	if (pendingRequest.outputIndex >= protocol::outputCount) {
		return;
	}

	auto& output = m_outputs[pendingRequest.outputIndex];
	output.pendingAck = false;

	if (rx[1] != protocol::resultOk) {
		output.dirty = true;
		return;
	}

	output.dirty = output.enabled != output.sentEnabled || output.frequencyHz != output.sentFrequencyHz ||
				   output.duty != output.sentDuty;
}

void OutputManager::requestOutput(size_t idx, bool enabled, uint32_t frequencyHz, uint16_t duty) {
	if (idx >= protocol::outputCount) {
		return;
	}

	if (duty > protocol::outputDutyMax) {
		duty = protocol::outputDutyMax;
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

} // namespace g0_extension

#endif
