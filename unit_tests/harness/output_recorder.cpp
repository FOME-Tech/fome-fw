/**
 * @file output_recorder.cpp
 */

#include "pch.h"

#include "harness/output_recorder.h"

#include <algorithm>
#include <cmath>

const char* describe(OutputKind kind) {
	switch (kind) {
		case OutputKind::Coil:
			return "coil";
		case OutputKind::TrailingCoil:
			return "trailing coil";
		case OutputKind::Injector:
			return "injector";
		case OutputKind::InjectorStage2:
			return "stage 2 injector";
		case OutputKind::Other:
			return "output";
	}

	return "?";
}

OutputRecorder::OutputRecorder() {
	for (size_t i = 0; i < MAX_CYLINDER_COUNT; i++) {
		watch(enginePins.coils[i], OutputKind::Coil, i);
		watch(enginePins.trailingCoils[i], OutputKind::TrailingCoil, i);
		watch(enginePins.injectors[i], OutputKind::Injector, i);
		watch(enginePins.injectorsStage2[i], OutputKind::InjectorStage2, i);
	}

	unitTestOutputPinObserver = [this](OutputPin& pin, bool state) { onWrite(pin, state); };
}

OutputRecorder::~OutputRecorder() {
	unitTestOutputPinObserver = nullptr;
}

void OutputRecorder::watch(OutputPin& pin, OutputKind kind, uint8_t index) {
	m_watched[&pin] = {kind, index};
}

void OutputRecorder::onWrite(OutputPin& pin, bool state) {
	auto it = m_watched.find(&pin);
	if (it == m_watched.end()) {
		return;
	}

	// The observer runs before the new value is stored, so this is the old state
	bool changed = pin.getLogicValue() != state;

	m_writes.push_back({
			.timeUs = getTimeNowUs(),
			.angle = m_angleSource ? m_angleSource() : NAN,
			.kind = it->second.kind,
			.index = it->second.index,
			.state = state,
			.changed = changed,
	});
}

std::vector<OutputPulse> OutputRecorder::pulses(OutputKind kind, int index) const {
	std::vector<OutputPulse> result;

	// Index in result of each output's currently open pulse, or -1
	int open[MAX_CYLINDER_COUNT];
	std::fill(std::begin(open), std::end(open), -1);

	for (const auto& w : m_writes) {
		if (w.kind != kind || (index >= 0 && w.index != index) || w.index >= MAX_CYLINDER_COUNT) {
			continue;
		}

		if (w.state && open[w.index] < 0) {
			open[w.index] = result.size();
			result.push_back({
					.kind = kind,
					.index = w.index,
					.startUs = w.timeUs,
					.startAngle = w.angle,
					.stillActive = true,
					.endUs = w.timeUs,
					.endAngle = w.angle,
			});
		} else if (!w.state && open[w.index] >= 0) {
			auto& pulse = result[open[w.index]];
			pulse.stillActive = false;
			pulse.endUs = w.timeUs;
			pulse.endAngle = w.angle;
			open[w.index] = -1;
		}
	}

	// Anything still active has been so until now
	for (auto& pulse : result) {
		if (pulse.stillActive) {
			pulse.endUs = getTimeNowUs();
			pulse.endAngle = m_angleSource ? m_angleSource() : NAN;
		}
	}

	return result;
}

size_t OutputRecorder::redundantOffCount(OutputKind kind) const {
	return std::count_if(m_writes.begin(), m_writes.end(), [kind](const OutputWrite& w) {
		return w.kind == kind && !w.state && !w.changed;
	});
}
