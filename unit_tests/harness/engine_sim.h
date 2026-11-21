/**
 * @file engine_sim.h
 *
 * A mechanical model of a spinning engine for unit tests.
 *
 * You describe how the crankshaft moves (constant speed, ramps, per-cylinder ripple, stall) and what
 * goes wrong (glitches, missing edges, a cam that moves), and the sim turns that in to correctly
 * timed crank and cam edges for the firmware, running the scheduler and the periodic callbacks in
 * between like the real ECU would.
 *
 * Because the sim knows the true crank angle at every instant, outputs recorded with OutputRecorder
 * are stamped with where the engine really was, not where the firmware thought it was.
 *
 * Typical use:
 *
 *   EngineTestHelper eth(engine_type_e::TEST_ENGINE);
 *   // ...configure, eth.setTriggerType(...)
 *   OutputRecorder recorder;
 *   EngineSim sim(eth, &recorder);
 *   sim.setRpm(1000);
 *   sim.runCycles(10);
 *   sim.rampRpm(4000, 0.5f);
 *   sim.runSeconds(1);
 *   EXPECT_TRUE(noOverdwell(recorder, 10));
 *
 * See engine_invariants.h for checks that work on the results.
 */

#pragma once

#include "engine_test_helper.h"
#include "harness/output_recorder.h"

#include <functional>
#include <vector>

// What the firmware believed, sampled just after each crank edge was delivered
struct SimSample {
	int64_t timeUs;
	// True mechanical crank angle, cumulative degrees since the sim was created
	double angle;
	float trueRpm;
	float reportedRpm;
	bool synced;
	bool fullSync;
	uint32_t triggerErrors;
	float phaseAdjustment;
	uint8_t camResyncs;
};

struct CamTooth {
	// Crank degrees within the 720 degree cycle at which the cam input goes high
	float riseAngle;
	// Crank degrees the input stays high for
	float width;
};

class EngineSim {
public:
	// Engine speed in RPM, as a function of time (seconds since the sim was created) and of
	// cumulative crank angle (degrees).
	using RpmFunction = std::function<float(double timeSec, double angle)>;

	// The trigger must already be configured: the crank wheel is built from the current trigger shape.
	// startAngle is where the crank is sitting to begin with, in the trigger definition's coordinates.
	explicit EngineSim(EngineTestHelper& eth, OutputRecorder* recorder = nullptr, double startAngle = 0);
	~EngineSim();

	EngineSim(const EngineSim&) = delete;
	EngineSim& operator=(const EngineSim&) = delete;

	// ---- How the engine moves ----

	// Constant speed from now on
	void setRpm(float rpm);
	// Linear ramp from the current speed to toRpm over the given time, then hold
	void rampRpm(float toRpm, float seconds);
	// Anything else
	void setRpmFunction(RpmFunction rpmFunction);
	// Speed variation within a revolution, as real engines have from compression and firing:
	// speed is multiplied by 1 + fraction * sin(angle * pulsesPerRev). Set fraction to 0 to disable.
	void setSpeedRipple(float fraction, float pulsesPerRev);

	// ---- Making time pass ----

	// Stops early (with a test failure) only if the engine isn't turning and there's no time limit
	void runDegrees(double degrees);
	void runRevolutions(double revolutions) {
		runDegrees(360 * revolutions);
	}
	void runCycles(double cycles) {
		runDegrees(720 * cycles);
	}
	void runSeconds(double seconds);

	// ---- Cams ----

	// Add a cam wheel on a cam input (index as for engineConfiguration->camInputs). Returns a handle.
	size_t addCam(int camInputIndex, const std::vector<CamTooth>& teeth);
	// Add a cam wheel matching the firmware's own definition of the configured VVT mode
	size_t addCamFromShape(int camInputIndex);
	// Move a cam: positive values make its teeth arrive later
	void setCamOffset(size_t cam, float crankDegrees);

	// ---- Things going wrong ----

	// A short pulse on a crank channel, starting afterDegrees from now
	void injectCrankGlitch(float afterDegrees, float widthDegrees = 0.5f, int channel = 0);
	// A short pulse on a cam input, starting afterDegrees from now
	void injectCamGlitch(size_t cam, float afterDegrees, float widthDegrees = 2);
	// Swallow the next count edges of a crank channel
	void dropCrankEdges(int count, int channel = 0);

	// ---- Reading the model ----

	// True crank angle right now (also valid part way between edges), cumulative degrees
	double angle() const;
	float trueRpm() const;
	int64_t nowUs() const;

	// Angle the crank wheel pattern repeats at: 360 for an ordinary crank wheel, 720 for a cam-speed one
	double wheelPeriod() const {
		return m_wheelPeriod;
	}

	// Where on the crank wheel the decoder's sync point is, [0, wheelPeriod())
	double syncAngle() const;

	// Where a cumulative mechanical angle is in the engine cycle, [0, 720), 0 = cylinder 1 TDC.
	// For crank-speed wheels the sim has no opinion on which revolution is which: compare modulo
	// wheelPeriod(), or look for consistency from cycle to cycle.
	double enginePhase(double angle) const;

	const std::vector<SimSample>& samples() const {
		return m_samples;
	}

	void clearSamples() {
		m_samples.clear();
	}

	// Called just after each crank edge has been delivered and sampled
	std::function<void(const SimSample&)> onCrankEdge;

	// Run the firmware's fast and slow periodic callbacks as time passes
	bool runPeriodicCallbacks = true;

private:
	struct State {
		// Seconds since the sim was created
		double t;
		// Cumulative crank degrees
		double a;
	};

	struct Edge {
		double angle;
		uint8_t channel;
		bool level;
	};

	struct Source {
		// -1 for the crank, otherwise the cam input index
		int camInputIndex;
		std::vector<Edge> edges;
		double period;
		double offset = 0;
		size_t next = 0;
		int64_t cycle = 0;

		double nextAngle() const {
			return cycle * period + edges[next].angle + offset;
		}
	};

	struct OneShot {
		double angle;
		// Index in to m_sources
		size_t source;
		uint8_t channel;
	};

	float rpmAt(double t, double a) const;
	// Move s forward until it reaches targetAngle or timeLimit. Returns true if the angle was reached.
	bool integrate(State& s, double targetAngle, double timeLimit) const;
	void run(double angleLimit, double timeLimit);
	void moveClockTo(double t);
	void positionSource(Source& source) const;
	size_t addSource(Source source);
	void fire(size_t source, uint8_t channel, bool level);
	void takeSample();

	EngineTestHelper& m_eth;
	OutputRecorder* const m_recorder;

	// Unit test clock value corresponding to m_state.t == 0
	const int64_t m_clockOriginUs;

	State m_state;
	double m_wheelPeriod;
	double m_tdcAngle;

	RpmFunction m_rpm;
	float m_rippleFraction = 0;
	float m_ripplePulsesPerRev = 0;

	std::vector<Source> m_sources;
	std::vector<OneShot> m_oneShots;
	// Current level of each source's channels, so glitches can toggle them
	std::vector<std::array<bool, 2>> m_levels;
	int m_dropCrank[2] = {0, 0};

	int64_t m_nextFastUs;
	int64_t m_nextSlowUs;

	std::vector<SimSample> m_samples;
};
