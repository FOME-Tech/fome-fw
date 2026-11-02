#include "pch.h"

#include "frequency_sensor.h"
#include "sensor_type.h"
#include "identity_func.h"

class FrequencySensorTest : public ::testing::Test {
public:
	FrequencySensorTest()
		: dut(SensorType::FuelEthanolPercent, MS2NT(50)) {}

	// Call after constructing the EngineTestHelper, which resets the sensor registry
	void init(uint8_t windowMs) {
		dut.initIfValid(Gpio::A0, identityFunction, windowMs);
	}

	// Advance time to absolute timeUs, running update() on every fast callback tick on the way
	void runUntil(EngineTestHelper& eth, double timeUs) {
		while (true) {
			double next = std::min(m_nextTickUs, timeUs);
			// Round the absolute time, not each step, so rounding doesn't accumulate
			double nextRounded = std::round(next);
			eth.moveTimeForwardUs(nextRounded - m_nowUs);
			m_nowUs = nextRounded;

			if (next < m_nextTickUs) {
				return;
			}

			dut.update(getTimeNowNt());
			m_nextTickUs += TickUs;

			if (m_track) {
				float v = Sensor::get(SensorType::FuelEthanolPercent).Value;
				m_min = std::min(m_min, v);
				m_max = std::max(m_max, v);
			}

			if (next == timeUs) {
				return;
			}
		}
	}

	// Edges at freqHz for durationSec, updating on every tick in between. jitterUs moves every
	// other edge late by that much.
	void run(EngineTestHelper& eth, double freqHz, double durationSec, double jitterUs = 0) {
		double periodUs = 1e6 / freqHz;
		// Straight after another run, continue its jitter-free edge grid, so that a late final edge
		// doesn't shift this run's edges
		double startUs = m_nowUs == m_lastEdgeUs ? m_gridUs : m_nowUs;
		int edges = durationSec * freqHz;

		for (int i = 1; i <= edges; i++) {
			runUntil(eth, startUs + i * periodUs + ((i % 2) ? jitterUs : 0));
			edge();
			m_gridUs = startUs + i * periodUs;
		}
	}

	void edge() {
		dut.onEdge(getTimeNowNt());
		m_lastEdgeUs = m_nowUs;
	}

	// Run ticks without edges for durationSec
	void idle(EngineTestHelper& eth, double durationSec) {
		runUntil(eth, m_nowUs + durationSec * 1e6);
	}

	float value() {
		auto s = Sensor::get(SensorType::FuelEthanolPercent);
		EXPECT_TRUE(s.Valid);
		return s.Value;
	}

	static constexpr double TickUs = 4000;

	FrequencySensor dut;

	double m_nowUs = 0;
	double m_nextTickUs = TickUs;
	double m_lastEdgeUs = 0;
	double m_gridUs = 0;

	// Range of values posted on every tick while tracking
	bool m_track = false;
	float m_min = 1e9;
	float m_max = -1e9;
};

TEST_F(FrequencySensorTest, NoEdgesIsZero) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	init(50);

	// Not updated yet
	EXPECT_FALSE(Sensor::get(SensorType::FuelEthanolPercent).Valid);

	idle(eth, 0.1);
	EXPECT_EQ(0, value());
}

TEST_F(FrequencySensorTest, HighFrequency) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	init(50);

	// Many edges per tick, many ticks per window
	run(eth, 3000, 1);
	EXPECT_NEAR(3000, value(), 0.5);
}

TEST_F(FrequencySensorTest, MidFrequency) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	init(50);

	// A few edges per window, fewer than one per tick
	run(eth, 92, 1);
	EXPECT_NEAR(92, value(), 0.01);
}

TEST_F(FrequencySensorTest, EdgesFurtherApartThanWindow) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	init(50);

	// 225ms between edges, a single tooth driveshaft sensor at walking pace. The window stretches
	// back to the previous edge.
	run(eth, 4.44, 3);
	EXPECT_NEAR(4.44, value(), 0.001);
}

TEST_F(FrequencySensorTest, WindowCancelsInteriorJitter) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	init(100);

	// Settle first
	run(eth, 97, 1, 300);

	// Every other edge 300us late: a raw edge-to-edge reading would be off by 3%. Only the ends of
	// the window matter, so on every tick the error is bounded by 300us over 100ms. 97Hz isn't a
	// multiple of the 4ms tick, so ticks land in the gap before a late edge too.
	m_track = true;
	run(eth, 97, 2, 300);
	EXPECT_NEAR(97, m_min, 97 * 0.0035);
	EXPECT_NEAR(97, m_max, 97 * 0.0035);
}

TEST_F(FrequencySensorTest, DecaysWhenEdgesStop) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	init(50);

	run(eth, 100, 1);
	EXPECT_NEAR(100, value(), 0.01);

	// A late edge doesn't pull the reading down until it is a whole period late
	runUntil(eth, m_lastEdgeUs + 19'000);
	EXPECT_NEAR(100, value(), 0.01);

	// Next edge is now 100ms late: bounded by two edges per 100ms
	runUntil(eth, m_lastEdgeUs + 100'000);
	EXPECT_NEAR(20, value(), 1);

	runUntil(eth, m_lastEdgeUs + 1'000'000);
	EXPECT_NEAR(2, value(), 0.01);

	// After 2 seconds without an edge, latch to a valid zero
	runUntil(eth, m_lastEdgeUs + 2'100'000);
	EXPECT_EQ(0, value());
}

TEST_F(FrequencySensorTest, ResumeAfterStop) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	init(50);

	run(eth, 100, 1);
	idle(eth, 3);
	EXPECT_EQ(0, value());

	// The first edge after a stop can't measure anything: the window must not stretch back across
	// the stop
	edge();
	idle(eth, 0.008);
	EXPECT_EQ(0, value());

	// The second one can
	runUntil(eth, m_lastEdgeUs + 20'000);
	edge();
	idle(eth, 0.004);
	EXPECT_NEAR(50, value(), 0.01);
}

TEST_F(FrequencySensorTest, AcrossTimerWrap) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	init(50);

	// The edge timestamps are only the lower 32 bits of the timer, run across their wrap
	double wrapUs = (double)(1ULL << 32) / US_TO_NT_MULTIPLIER;
	// The fixture's clock is relative to wherever the test helper started time
	double baseUs = NT2US((double)getTimeNowNt()) - m_nowUs;
	runUntil(eth, wrapUs - 500'000 - baseUs);

	for (int i = 0; i < 10; i++) {
		run(eth, 1000, 0.1);
		EXPECT_NEAR(1000, value(), 0.5);
	}

	EXPECT_GT(getTimeNowNt(), (int64_t)(1ULL << 32));
}

TEST_F(FrequencySensorTest, StepChangeSettlesWithinWindow) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	init(50);

	run(eth, 500, 1);
	EXPECT_NEAR(500, value(), 0.1);

	// One window (plus a tick and a stride of slack) after a step, only new edges are averaged
	run(eth, 1000, 0.062);
	EXPECT_NEAR(1000, value(), 1);
}
