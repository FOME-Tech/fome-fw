/**
 * @file engine_sim.cpp
 */

#include "pch.h"

#include "harness/engine_sim.h"

#include "main_loop.h"

#include <algorithm>
#include <cmath>
#include <limits>

static constexpr double noLimit = std::numeric_limits<double>::infinity();

// Largest angle the integrator covers in one step. Small enough that speed ripple and ramps are
// followed closely, and that reading the angle between edges is accurate to a small fraction of a degree.
static constexpr double maxStepDegrees = 0.25;

// Below this the engine is considered stopped, and no amount of waiting will produce the next edge
static constexpr float stalledRpm = 0.01f;

static constexpr int64_t fastPeriodUs = 1000 * FAST_CALLBACK_PERIOD_MS;
static constexpr int64_t slowPeriodUs = 1000 * SLOW_CALLBACK_PERIOD_MS;

// Level of a channel just before the given position on the wheel
template <typename TEdges>
static bool levelBefore(const TEdges& edges, uint8_t channel, double position) {
	bool level = false;
	bool found = false;

	for (const auto& e : edges) {
		if (e.channel != channel) {
			continue;
		}

		if (e.angle < position) {
			level = e.level;
			found = true;
		}
	}

	if (!found) {
		// Nothing earlier in this revolution, so it's wherever the previous revolution left it
		for (const auto& e : edges) {
			if (e.channel == channel) {
				level = e.level;
			}
		}
	}

	return level;
}

EngineSim::EngineSim(EngineTestHelper& eth, OutputRecorder* recorder, double startAngle)
	: m_eth(eth)
	, m_recorder(recorder)
	, m_clockOriginUs(getTimeNowUs())
	, m_state{0, startAngle} {
	const auto& shape = engine->triggerCentral.triggerShape;

	EXPECT_FALSE(shape.shapeDefinitionError) << "EngineSim needs a valid trigger shape";

	m_wheelPeriod = shape.getCycleDuration();

	// Build the crank wheel from the firmware's own definition of the trigger
	Source crank;
	crank.camInputIndex = -1;
	crank.period = m_wheelPeriod;

	const int size = shape.getSize();
	for (int i = 0; i < size; i++) {
		int prev = (i + size - 1) % size;

		for (uint8_t channel = 0; channel < 2; channel++) {
			bool before = shape.wave.getChannelState(channel, prev);
			bool after = shape.wave.getChannelState(channel, i);

			if (before != after) {
				crank.edges.push_back({shape.getAngle(i), channel, after});
			}
		}
	}

	addSource(crank);

	m_nextFastUs = m_clockOriginUs + fastPeriodUs;
	m_nextSlowUs = m_clockOriginUs + slowPeriodUs;

	setRpm(0);

	if (m_recorder) {
		m_recorder->setAngleSource([this]() { return angle(); });
	}
}

EngineSim::~EngineSim() {
	if (m_recorder) {
		m_recorder->setAngleSource(nullptr);
	}
}

// ---- Speed ----

float EngineSim::rpmAt(double t, double a) const {
	float rpm = m_rpm(t, a);

	if (m_rippleFraction != 0) {
		rpm *= 1 + m_rippleFraction * std::sin(a * (M_PI / 180) * m_ripplePulsesPerRev);
	}

	return rpm;
}

void EngineSim::setRpm(float rpm) {
	m_rpm = [rpm](double, double) { return rpm; };
}

void EngineSim::rampRpm(float toRpm, float seconds) {
	double t0 = 1e-6 * (nowUs() - m_clockOriginUs);
	float fromRpm = m_rpm(t0, angle());

	m_rpm = [=](double t, double) {
		if (seconds <= 0 || t >= t0 + seconds) {
			return toRpm;
		}

		return (float)(fromRpm + (toRpm - fromRpm) * (t - t0) / seconds);
	};
}

void EngineSim::setRpmFunction(RpmFunction rpmFunction) {
	m_rpm = rpmFunction;
}

void EngineSim::setSpeedRipple(float fraction, float pulsesPerRev) {
	m_rippleFraction = fraction;
	m_ripplePulsesPerRev = pulsesPerRev;
}

// ---- Model ----

bool EngineSim::integrate(State& s, double targetAngle, double timeLimit) const {
	while (s.a < targetAngle) {
		double step = std::min(maxStepDegrees, targetAngle - s.a);

		// Estimate with the speed here, then refine with the speed at the middle of the step
		float rpm = rpmAt(s.t, s.a);
		if (rpm > stalledRpm) {
			double dtEstimate = step / (6.0 * rpm);
			rpm = rpmAt(s.t + dtEstimate / 2, s.a + step / 2);
		}

		if (rpm <= stalledRpm) {
			// Not turning: only the passage of time can get us out of this
			if (timeLimit == noLimit) {
				return false;
			}

			// Step time forward to see whether the speed function picks up again
			double dt = std::min(1e-3, timeLimit - s.t);
			s.t += dt;

			if (s.t >= timeLimit) {
				s.t = timeLimit;
				return false;
			}

			continue;
		}

		double degPerSec = 6.0 * rpm;
		double dt = step / degPerSec;

		if (s.t + dt > timeLimit) {
			// Out of time part way through this step
			s.a += degPerSec * (timeLimit - s.t);
			s.t = timeLimit;
			return false;
		}

		s.t += dt;
		s.a += step;
	}

	// Avoid creeping past the target
	s.a = targetAngle;
	return true;
}

int64_t EngineSim::nowUs() const {
	return getTimeNowUs();
}

double EngineSim::angle() const {
	double now = 1e-6 * (nowUs() - m_clockOriginUs);

	if (now <= m_state.t) {
		return m_state.a;
	}

	// Somewhere after the last committed point: follow the speed profile to the current time
	State s = m_state;
	integrate(s, noLimit, now);
	return s.a;
}

float EngineSim::trueRpm() const {
	return rpmAt(1e-6 * (nowUs() - m_clockOriginUs), angle());
}

double EngineSim::syncAngle() const {
	const auto& shape = engine->triggerCentral.triggerShape;

	return shape.getAngle(shape.triggerShapeSynchPointIndex.value_or(0) % shape.getSize());
}

double EngineSim::enginePhase(double angle) const {
	const auto& shape = engine->triggerCentral.triggerShape;

	// The decoder's zero is the sync point; cylinder 1 TDC is tdcPosition + the user's offset after that
	double tdc = syncAngle() + shape.tdcPosition + engineConfiguration->globalTriggerAngleOffset;

	double phase = std::fmod(angle - tdc, 720.0);
	if (phase < 0) {
		phase += 720;
	}

	return phase;
}

// ---- Edge sources ----

void EngineSim::positionSource(Source& source) const {
	if (source.edges.empty()) {
		return;
	}

	double a = m_state.a;

	source.cycle = (int64_t)std::floor((a - source.offset) / source.period);
	source.next = 0;

	while (source.nextAngle() < a) {
		source.next++;

		if (source.next == source.edges.size()) {
			source.next = 0;
			source.cycle++;
		}
	}
}

size_t EngineSim::addSource(Source source) {
	std::stable_sort(
			source.edges.begin(), source.edges.end(), [](const Edge& l, const Edge& r) { return l.angle < r.angle; });

	positionSource(source);

	double position = std::fmod(m_state.a - source.offset, source.period);
	if (position < 0) {
		position += source.period;
	}

	m_levels.push_back({levelBefore(source.edges, 0, position), levelBefore(source.edges, 1, position)});
	m_sources.push_back(source);

	return m_sources.size() - 1;
}

size_t EngineSim::addCam(int camInputIndex, const std::vector<CamTooth>& teeth) {
	Source cam;
	cam.camInputIndex = camInputIndex;
	cam.period = 720;

	for (const auto& tooth : teeth) {
		double rise = std::fmod(tooth.riseAngle, 720.0);
		if (rise < 0) {
			rise += 720;
		}

		double fall = std::fmod(rise + tooth.width, 720.0);

		cam.edges.push_back({rise, 0, true});
		cam.edges.push_back({fall, 0, false});
	}

	return addSource(cam);
}

size_t EngineSim::addCamFromShape(int camInputIndex) {
	const auto& shape = engine->triggerCentral.vvtShape[camInputIndex % CAMS_PER_BANK];

	Source cam;
	cam.camInputIndex = camInputIndex;
	cam.period = 720;

	const int size = shape.getSize();
	EXPECT_GT(size, 0) << "VVT mode of cam input " << camInputIndex << " has no trigger shape, use addCam()";

	for (int i = 0; i < size; i++) {
		int prev = (i + size - 1) % size;
		bool before = shape.wave.getChannelState(0, prev);
		bool after = shape.wave.getChannelState(0, i);

		if (before != after) {
			cam.edges.push_back({shape.getAngle(i), 0, after});
		}
	}

	return addSource(cam);
}

void EngineSim::setCamOffset(size_t cam, float crankDegrees) {
	// The next edge keeps its place in the sequence: it just arrives earlier or later than it would have
	m_sources[cam].offset = crankDegrees;
}

// ---- Faults ----

void EngineSim::injectCrankGlitch(float afterDegrees, float widthDegrees, int channel) {
	double start = angle() + afterDegrees;
	m_oneShots.push_back({start, 0, (uint8_t)channel});
	m_oneShots.push_back({start + widthDegrees, 0, (uint8_t)channel});
}

void EngineSim::injectCamGlitch(size_t cam, float afterDegrees, float widthDegrees) {
	double start = angle() + afterDegrees;
	m_oneShots.push_back({start, cam, 0});
	m_oneShots.push_back({start + widthDegrees, cam, 0});
}

void EngineSim::dropCrankEdges(int count, int channel) {
	m_dropCrank[channel] += count;
}

// ---- Running ----

void EngineSim::moveClockTo(double t) {
	int64_t targetUs = std::max<int64_t>(m_clockOriginUs + std::llround(t * 1e6), getTimeNowUs());

	// Runs scheduled events in order up to the given time. Unlike the plain test helper, the clock
	// never goes backwards for an event that was scheduled late: like on the real ECU, it runs "now".
	auto runSchedulerTo = [this](int64_t untilUs) {
		while (true) {
			scheduling_s* head = engine->scheduler.getHead();

			if (!head || head->momentX > untilUs) {
				break;
			}

			setTimeNowUs(std::max<int64_t>(getTimeNowUs(), head->momentX));
			engine->scheduler.executeAll(getTimeNowUs());
		}

		setTimeNowUs(untilUs);
	};

	while (runPeriodicCallbacks) {
		int64_t next = std::min(m_nextFastUs, m_nextSlowUs);

		if (next > targetUs) {
			break;
		}

		runSchedulerTo(next);

		if (m_nextFastUs == next) {
			engine->periodicFastCallback();
			m_nextFastUs += fastPeriodUs;
		}

		if (m_nextSlowUs == next) {
			engine->periodicSlowCallback();
			m_nextSlowUs += slowPeriodUs;
		}

		// Anything those scheduled for right now
		engine->scheduler.executeAll(getTimeNowUs());
	}

	runSchedulerTo(targetUs);
}

void EngineSim::fire(size_t source, uint8_t channel, bool level) {
	m_levels[source][channel] = level;

	efitick_t nowNt = getTimeNowNt();
	int camInputIndex = m_sources[source].camInputIndex;

	if (camInputIndex < 0) {
		if (m_dropCrank[channel] > 0) {
			m_dropCrank[channel]--;
			return;
		}

		hwHandleShaftSignal(channel, level, nowNt);
	} else {
		hwHandleVvtCamSignal(level, nowNt, camInputIndex);
	}

	// Events scheduled for a time that has already passed run immediately on the real ECU
	engine->scheduler.executeAll(getTimeNowUs());

	if (camInputIndex < 0) {
		takeSample();
	}
}

void EngineSim::takeSample() {
	const auto& decoder = engine->triggerCentral.triggerState;

	m_samples.push_back({
			.timeUs = nowUs(),
			.angle = m_state.a,
			.trueRpm = rpmAt(m_state.t, m_state.a),
			.reportedRpm = Sensor::getOrZero(SensorType::Rpm),
			.synced = decoder.getShaftSynchronized(),
			.fullSync = decoder.hasSynchronizedPhase(),
			.triggerErrors = decoder.triggerErrorCounter,
			.phaseAdjustment = decoder.m_phaseAdjustment,
			.camResyncs = decoder.m_camResyncCounter,
	});

	if (onCrankEdge) {
		onCrankEdge(m_samples.back());
	}
}

void EngineSim::run(double angleLimit, double timeLimit) {
	while (true) {
		// Find whatever happens next
		double nextAngle = noLimit;
		int nextSource = -1;
		int nextOneShot = -1;

		for (size_t i = 0; i < m_sources.size(); i++) {
			if (!m_sources[i].edges.empty() && m_sources[i].nextAngle() < nextAngle) {
				nextAngle = m_sources[i].nextAngle();
				nextSource = i;
			}
		}

		for (size_t i = 0; i < m_oneShots.size(); i++) {
			if (m_oneShots[i].angle < nextAngle) {
				nextAngle = m_oneShots[i].angle;
				nextOneShot = i;
			}
		}

		// A cam that was just moved earlier may have an edge that's already behind us: it happens now
		nextAngle = std::max(nextAngle, m_state.a);

		bool edgeIsNext = nextAngle <= angleLimit;

		State s = m_state;
		bool reached = integrate(s, edgeIsNext ? nextAngle : angleLimit, timeLimit);

		moveClockTo(s.t);
		m_state = s;

		if (!reached) {
			if (timeLimit == noLimit) {
				ADD_FAILURE() << "EngineSim: asked to run to an angle, but the engine isn't turning";
			}

			return;
		}

		if (!edgeIsNext) {
			return;
		}

		if (nextOneShot >= 0) {
			auto glitch = m_oneShots[nextOneShot];
			m_oneShots.erase(m_oneShots.begin() + nextOneShot);

			fire(glitch.source, glitch.channel, !m_levels[glitch.source][glitch.channel]);
		} else {
			auto& source = m_sources[nextSource];
			auto edge = source.edges[source.next];

			source.next++;
			if (source.next == source.edges.size()) {
				source.next = 0;
				source.cycle++;
			}

			fire(nextSource, edge.channel, edge.level);
		}
	}
}

void EngineSim::runDegrees(double degrees) {
	run(angle() + degrees, noLimit);
}

void EngineSim::runSeconds(double seconds) {
	run(noLimit, 1e-6 * (nowUs() - m_clockOriginUs) + seconds);
}
