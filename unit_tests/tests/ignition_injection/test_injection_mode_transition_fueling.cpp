/*
 * @file test_injection_mode_transition_fueling.cpp
 *
 * Checks that every cylinder receives exactly one cycle's worth of fuel per intake while the
 * injection mode switches from batch or simultaneous to sequential, regardless of where in the
 * cycle the switch happens and regardless of whether the pre-sync phase guess was right.
 *
 * Fuel is accounted per injector: with end-of-injection timing ending before the intake stroke,
 * the fuel feeding an intake is everything that injector received since its own previous
 * injection event.
 */

#include "pch.h"

#include <set>
#include <vector>

namespace {

constexpr float testRpm = 300;
constexpr int teethPerRev = 36;

enum class Scenario {
	// Sequential cranking, batch until cam sync, cam syncs while cranking
	BatchToSeqCranking,
	// Sequential running, batch until cam sync, cam syncs while running
	BatchToSeqRunning,
	// Simultaneous cranking, sequential running, transition at the end of cranking
	SimultaneousToSeq,
};

// Times are as stored by the unit test executor, which keeps scheduled events in microseconds
struct Pulse {
	efitick_t start;
	efitick_t end;
	uint16_t mask;
};

class PulseRecorder {
public:
	// Record any injections scheduled since the last scan
	void scan() {
		auto& sched = engine->scheduler;

		for (int i = 0; i < sched.size(); i++) {
			auto open = sched.getForUnitTest(i);
			if (open->action.getCallback() != (void (*)(void*))startInjection) {
				continue;
			}

			void* arg = open->action.getArgument();
			auto key = std::make_pair(open->momentX, (uintptr_t)arg);
			if (m_seen.count(key)) {
				continue;
			}
			m_seen.insert(key);

			// Find the matching close
			efitick_t closeTime = 0;
			for (int j = 0; j < sched.size(); j++) {
				auto close = sched.getForUnitTest(j);
				if (close->action.getCallback() == (void (*)(void*))endInjection &&
					close->action.getArgument() == arg && close->momentX > open->momentX) {
					if (closeTime == 0 || close->momentX < closeTime) {
						closeTime = close->momentX;
					}
				}
			}
			ASSERT_NE(closeTime, 0) << "injection without matching close";

			auto ctx = bit_cast<InjectorContext>(arg);
			pulses.push_back({open->momentX, closeTime, ctx.outputsMask});
		}
	}

	std::vector<Pulse> pulses;

private:
	std::set<std::pair<efitick_t, uintptr_t>> m_seen;
};

class TransitionSim {
public:
	TransitionSim(Scenario scenario)
		: m_scenario(scenario) {
		// Crank-mounted 36-1 wheel, plus a cam input: the trigger needs phase info and expects to get it
		setCrankOperationMode();
		engineConfiguration->vvtMode[0] = VVT_SINGLE_TOOTH;
		engineConfiguration->camInputs[0] = Gpio::A10;

		setupSimpleTestEngineWithMaf(&eth, IM_SEQUENTIAL, trigger_type_e::TT_TOOTHED_WHEEL_36_1);
		engine->tdcMarkEnabled = false;

		// Port injection ending before the intake stroke
		engineConfiguration->injectionTimingMode = InjectionTimingMode::End;
		setTable(config->injectionPhase, -400.0f);

		// Constant fuel mass for the whole test, so every pulse is directly comparable. Small
		// injectors so pulses are a few ms long.
		engineConfiguration->injector.flow = 200;
		EXPECT_CALL(*eth.mockAirmass, getAirmass(::testing::_, ::testing::_))
				.WillRepeatedly(Return(AirmassResult{0.1f, 50.0f}));
		engineConfiguration->useRunningMathForCranking = true;
		setArrayValues(config->crankingCycleCoef, 1.0f);
		setArrayValues(config->crankingFuelCoef, 1.0f);
		setArrayValues(config->crankingTpsCoef, 1.0f);
		engineConfiguration->flexCranking = false;
		setArrayValues(config->cltFuelCorr, 1.0f);
		setArrayValues(config->iatFuelCorr, 1.0f);
		engineConfiguration->postCrankingFuelUseTable = false;
		engineConfiguration->postCrankingFactor = 1.0f;

		switch (scenario) {
			case Scenario::BatchToSeqCranking:
				engineConfiguration->crankingInjectionMode = IM_SEQUENTIAL;
				// stay cranking the whole time
				engineConfiguration->cranking.rpm = 999;
				break;
			case Scenario::BatchToSeqRunning:
				// running the whole time
				engineConfiguration->cranking.rpm = 100;
				break;
			case Scenario::SimultaneousToSeq:
				engineConfiguration->crankingInjectionMode = IM_SIMULTANEOUS;
				// start out cranking, the test drops the threshold later to end cranking
				engineConfiguration->cranking.rpm = 999;
				break;
		}
	}

	// Fire one crank revolution: 35 teeth 10 degrees apart, then the missing tooth gap
	void fireRevolution(int syncAfterTooth = -1, bool wrongGuess = false) {
		float msPerDegree = 60000.0f / testRpm / 360;
		float halfTooth = 5 * msPerDegree;

		for (int tooth = 0; tooth < teethPerRev - 1; tooth++) {
			// first tooth after the gap is 15 degrees after the previous fall
			eth.smartFireRise(tooth == 0 ? 3 * halfTooth : halfTooth);
			recorder.scan();
			eth.smartFireFall(halfTooth);
			recorder.scan();

			if (tooth == syncAfterTooth) {
				// Pretend the cam just gave us phase. Choose the remainder so the pre-sync guess
				// was right (no shift) or wrong (360 degree shift).
				int current = engine->triggerCentral.triggerState.getCrankSynchronizationCounter() % 2;
				int remainder = wrongGuess ? (1 - current) : current;
				phaseShift = engine->triggerCentral.syncAndReport(2, remainder);
			}
		}
	}

	void run(int syncAfterTooth, bool wrongGuess) {
		// Spin up and run for a while without phase info
		for (int rev = 0; rev < 6; rev++) {
			fireRevolution();
		}

		ASSERT_TRUE(engine->triggerCentral.triggerState.expectDisambiguation());

		// Sanity check the crank wheel decodes cleanly: one crank sync per revolution
		{
			int before = engine->triggerCentral.triggerState.getCrankSynchronizationCounter();
			fireRevolution();
			ASSERT_EQ(before + 1, engine->triggerCentral.triggerState.getCrankSynchronizationCounter());
			ASSERT_NEAR(testRpm, Sensor::getOrZero(SensorType::Rpm), 1);
		}

		if (m_scenario == Scenario::SimultaneousToSeq) {
			// Get phase early, then crank for a while before ending cranking
			fireRevolution(syncAfterTooth, wrongGuess);
			for (int rev = 0; rev < 4; rev++) {
				fireRevolution();
			}

			ASSERT_TRUE(engine->rpmCalculator.isCranking());
			ASSERT_EQ(IM_SIMULTANEOUS, getCurrentInjectionMode());
			transitionTime = getTimeNowUs();

			// End cranking: next RPM update takes us to running
			engineConfiguration->cranking.rpm = 100;
		} else {
			ASSERT_EQ(IM_BATCH, getCurrentInjectionMode());
			transitionTime = getTimeNowUs();

			// Cam syncs part way through this revolution
			fireRevolution(syncAfterTooth, wrongGuess);
		}

		for (int rev = 0; rev < 12; rev++) {
			fireRevolution();
		}

		ASSERT_EQ(IM_SEQUENTIAL, getCurrentInjectionMode());
		ASSERT_EQ(m_scenario == Scenario::BatchToSeqCranking, engine->rpmCalculator.isCranking());

		// Let the last pulses finish, then make sure nothing is stuck open
		eth.moveTimeForwardAndInvokeEventsUs(MS2US(1000));
		for (size_t i = 0; i < 4; i++) {
			EXPECT_EQ(0, enginePins.injectors[i].getOverlappingCounter()) << "injector " << i;
		}
	}

	// Fuel (injector open time, in scheduler ticks) received by each injector in each intake window,
	// from two cycles before its first sequential pulse to three cycles after
	std::vector<std::vector<float>> fuelPerWindow(float& fullPulse) const {
		std::vector<std::vector<float>> result;
		fullPulse = 0;

		for (int inj = 0; inj < 4; inj++) {
			uint16_t ownMask = 1 << inj;

			// The last two pulses are steady-state sequential: they give the size of a full cycle of
			// fuel, and the length of an engine cycle
			const Pulse* lastSeq = nullptr;
			const Pulse* prevSeq = nullptr;
			for (auto& p : recorder.pulses) {
				if (p.mask == ownMask) {
					prevSeq = lastSeq;
					lastSeq = &p;
				}
			}
			EXPECT_NE(prevSeq, nullptr) << "injector " << inj << " never went sequential";
			if (!prevSeq) {
				result.push_back({});
				continue;
			}
			fullPulse = lastSeq->end - lastSeq->start;
			efidur_t cycle = lastSeq->end - prevSeq->end;

			// First full-size sequential pulse after the transition. A non-sequential pulse can also
			// drive only this injector, but it will be a fraction of the size.
			const Pulse* firstSeq = nullptr;
			for (auto& p : recorder.pulses) {
				if (p.start > transitionTime && p.mask == ownMask &&
					std::abs((float)(p.end - p.start) - fullPulse) < 0.02f * fullPulse) {
					firstSeq = &p;
					break;
				}
			}
			EXPECT_NE(firstSeq, nullptr);
			if (!firstSeq) {
				result.push_back({});
				continue;
			}

			// Window boundaries 90 degrees after this injector's own end of injection, so no pulse
			// lands on a boundary
			efitick_t anchor = firstSeq->end + cycle / 8;

			std::vector<float> windows;
			for (int k = -2; k <= 3; k++) {
				efitick_t windowStart = anchor + (k - 1) * cycle;
				efitick_t windowEnd = anchor + k * cycle;

				float fuel = 0;
				for (auto& p : recorder.pulses) {
					if ((p.mask & ownMask) && p.end > windowStart && p.end <= windowEnd) {
						fuel += p.end - p.start;
					}
				}
				windows.push_back(fuel);
			}

			result.push_back(windows);
		}

		return result;
	}

	EngineTestHelper eth{engine_type_e::TEST_ENGINE};
	PulseRecorder recorder;
	angle_t phaseShift = -1;
	efitick_t transitionTime = 0;

private:
	Scenario m_scenario;
};

std::vector<std::vector<float>> runTransition(Scenario scenario, int syncAfterTooth, bool wrongGuess) {
	TransitionSim sim(scenario);
	sim.run(syncAfterTooth, wrongGuess);

	EXPECT_FLOAT_EQ(wrongGuess ? 360 : 0, sim.phaseShift) << "phase guess wasn't the case under test";

	float fullPulse;
	auto windows = sim.fuelPerWindow(fullPulse);
	EXPECT_GT(fullPulse, 0);

	// Every cylinder gets exactly one cycle of fuel per intake, before, during and after the switch
	for (size_t inj = 0; inj < windows.size(); inj++) {
		for (size_t k = 0; k < windows[inj].size(); k++) {
			EXPECT_NEAR(fullPulse, windows[inj][k], 0.02f * fullPulse)
					<< "injector " << inj << " window " << (int)k - 2 << " (0 = first sequential intake)"
					<< (wrongGuess ? ", wrong phase guess" : ", right phase guess") << ", sync after tooth "
					<< syncAfterTooth;
		}
	}

	return windows;
}

void checkRightAndWrongGuessMatch(Scenario scenario) {
	// Vary where in the cycle phase sync lands relative to the injection events
	for (int syncAfterTooth : {3, 12, 21, 30}) {
		auto right = runTransition(scenario, syncAfterTooth, /*wrongGuess=*/false);
		auto wrong = runTransition(scenario, syncAfterTooth, /*wrongGuess=*/true);

		ASSERT_EQ(right.size(), wrong.size());
		for (size_t inj = 0; inj < right.size(); inj++) {
			ASSERT_EQ(right[inj].size(), wrong[inj].size());
			for (size_t k = 0; k < right[inj].size(); k++) {
				EXPECT_NEAR(right[inj][k], wrong[inj][k], 0.02f * right[inj][k])
						<< "injector " << inj << " window " << (int)k - 2 << ", sync after tooth " << syncAfterTooth;
			}
		}
	}
}

} // namespace

TEST(InjectionModeTransition, BatchToSequentialWhileCranking) {
	checkRightAndWrongGuessMatch(Scenario::BatchToSeqCranking);
}

TEST(InjectionModeTransition, BatchToSequentialWhileRunning) {
	checkRightAndWrongGuessMatch(Scenario::BatchToSeqRunning);
}

TEST(InjectionModeTransition, SimultaneousCrankingToSequentialRunning) {
	checkRightAndWrongGuessMatch(Scenario::SimultaneousToSeq);
}
