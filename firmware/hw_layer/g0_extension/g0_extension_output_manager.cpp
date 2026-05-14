#include "g0_extension_io_impl.h"

#if EFI_PROD_CODE

namespace g0_extension {
void OutputManager::prepareRequest(protocol::AppFrame& tx, PendingRequest& nextRequest) {
	nextRequest = {};

	for (size_t i = 0; i < protocol::outputCount; i++) {
		if (!m_outputs[i].dirty || m_outputs[i].pendingAck) {
			continue;
		}

		const auto& output = m_outputs[i];
		nextRequest.outputIndex = i;

		if (output.enabled) {
			tx.setOutputRequest.command = protocol::cmdSetOutput;
			tx.setOutputRequest.output = static_cast<uint8_t>(i + 1);
			tx.setOutputRequest.frequencyHz = output.frequencyHz;
			tx.setOutputRequest.duty = output.duty;
			nextRequest.type = RequestType::SetOutput;
		} else {
			tx.indexedRequest.command = protocol::cmdDisableOutput;
			tx.indexedRequest.index = static_cast<uint8_t>(i + 1);
			nextRequest.type = RequestType::DisableOutput;
		}

		m_outputs[i].pendingAck = true;
		m_outputs[i].sentEnabled = output.enabled;
		m_outputs[i].sentFrequencyHz = output.frequencyHz;
		m_outputs[i].sentDuty = output.duty;
		return;
	}
}

void OutputManager::parseAck(const PendingRequest& pendingRequest, const protocol::AppFrame& rx) {
	if (pendingRequest.outputIndex >= protocol::outputCount) {
		return;
	}

	auto& output = m_outputs[pendingRequest.outputIndex];
	output.pendingAck = false;

	if (rx.responseHeader.result != protocol::resultOk) {
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
