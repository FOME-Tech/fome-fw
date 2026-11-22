/**
 * @file output_recorder.h
 *
 * Records every write to the engine's output pins (coils, injectors, and anything else a test asks
 * for), so a test can check what the outputs actually did rather than what was scheduled.
 *
 * It knows nothing about how the engine is being driven: use it with EngineSim, a CSV trigger replay,
 * or hand-fired teeth alike.
 */

#pragma once

#include "efi_output.h"

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

enum class OutputKind : uint8_t {
	Coil,
	TrailingCoil,
	Injector,
	InjectorStage2,
	Other,
};

const char* describe(OutputKind kind);

// One write to a watched pin
struct OutputWrite {
	int64_t timeUs;
	// Mechanical crank angle (cumulative degrees) at the time of the write, NAN if there's no angle source
	double angle;
	OutputKind kind;
	// Cylinder number for engine outputs, or whatever was passed to watch()
	uint8_t index;
	bool state;
	// False if the pin was already in this state (for example, firing a coil that was never charged)
	bool changed;
};

// One period of a pin being active
struct OutputPulse {
	OutputKind kind;
	uint8_t index;
	int64_t startUs;
	double startAngle;
	// The pin was still active when the pulses were extracted: endUs/endAngle are "now", not a real edge
	bool stillActive;
	int64_t endUs;
	double endAngle;

	float durationMs() const {
		return (endUs - startUs) * 1e-3f;
	}
};

class OutputRecorder {
public:
	// Starts recording coils, trailing coils, injectors and stage 2 injectors immediately
	OutputRecorder();
	~OutputRecorder();

	OutputRecorder(const OutputRecorder&) = delete;
	OutputRecorder& operator=(const OutputRecorder&) = delete;

	// Record some other pin too
	void watch(OutputPin& pin, OutputKind kind, uint8_t index);

	// Supplies the mechanical angle recorded alongside each write. EngineSim installs itself here.
	void setAngleSource(std::function<double()> source) {
		m_angleSource = source;
	}

	// Forget everything recorded so far (pins that are currently active are remembered as such)
	void clear() {
		m_writes.clear();
	}

	const std::vector<OutputWrite>& writes() const {
		return m_writes;
	}

	// All recorded active periods of one kind of output, in order of start time.
	// Pass an index to only get one cylinder's.
	std::vector<OutputPulse> pulses(OutputKind kind, int index = -1) const;

	// Number of writes that set an output inactive when it already was
	size_t redundantOffCount(OutputKind kind) const;

private:
	struct Watched {
		OutputKind kind;
		uint8_t index;
	};

	void onWrite(OutputPin& pin, bool state);

	std::unordered_map<const OutputPin*, Watched> m_watched;
	std::vector<OutputWrite> m_writes;
	std::function<double()> m_angleSource;
};
