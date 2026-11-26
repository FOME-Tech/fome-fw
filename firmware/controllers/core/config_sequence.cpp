/**
 * @file config_sequence.cpp
 */

#include "config_sequence.h"

void ConfigSequence::boot(BootPhase phase) {
	for (size_t i = 0; i < m_count; i++) {
		const auto& step = m_steps[i];

		if (step.phase == phase && step.start) {
			step.start(nullptr);
		}
	}

	m_bootedPhases |= 1 << static_cast<uint8_t>(phase);

	updateRebootRequired();
}

void ConfigSequence::beginChange(const engine_configuration_s* prev) {
	for (size_t i = m_count; i-- > 0;) {
		const auto& step = m_steps[i];

		if (appliesChanges(step) && step.stop) {
			step.stop(prev);
		}
	}
}

void ConfigSequence::finishChange(const engine_configuration_s* prev) {
	for (size_t i = 0; i < m_count; i++) {
		const auto& step = m_steps[i];

		if (appliesChanges(step) && step.start) {
			step.start(prev);
		}
	}

	updateRebootRequired();
}

void ConfigSequence::updateRebootRequired() {
	bool required = false;

	for (size_t i = 0; i < m_count; i++) {
		const auto& step = m_steps[i];

		if (isBooted(step) && step.when == ApplyWhen::RebootOnly && step.needsReboot) {
			required |= step.needsReboot();
		}
	}

	m_rebootRequired = required;
}
