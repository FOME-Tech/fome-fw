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

#include "harness/engine_invariants.h"

#include <cmath>
#include <vector>

namespace {

constexpr float testRpm = 300;

enum class Scenario {
	// Sequential cranking, batch until cam sync, cam syncs while cranking
	BatchToSeqCranking,
	// Sequential running, batch until cam sync, cam syncs while running
	BatchToSeqRunning,
	// Simultaneous cranking, sequential running, transition at the end of cranking
	SimultaneousToSeq,
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

		m_sim = std::make_unique<EngineSim>(eth, &recorder);
	}

	// Fit a cam whose tooth arrives camToothAfterSync degrees after a crank sync point, on whichever
	// of the two crank revolutions makes the firmware's pre-sync guess of engine phase right or wrong
	void fitCam(float camToothAfterSync, bool wrongGuess) {
		auto& sim = *m_sim;

		// Stop just after a crank sync point, so the revolution counter isn't about to change under us
		double sinceSync = std::fmod(sim.angle() - sim.syncAngle(), 360.0);
		sim.runDegrees(360 - sinceSync + 1);

		// The guess is right if the cam tooth lands on an even-numbered revolution
		bool evenRevolutionNow = engine->triggerCentral.triggerState.getCrankSynchronizationCounter() % 2 == 0;
		bool wantEvenRevolution = !wrongGuess;

		double revolutionStart = sim.angle() - 1;
		if (evenRevolutionNow != wantEvenRevolution) {
			revolutionStart += 360;
		}

		// The revolution that's already under way has had its chance if the tooth position is behind us
		if (revolutionStart + camToothAfterSync <= sim.angle()) {
			revolutionStart += 720;
		}

		sim.addCam(0, {{(float)std::fmod(revolutionStart + camToothAfterSync, 720.0), 20}});
	}

	void run(float camToothAfterSync, bool wrongGuess) {
		auto& sim = *m_sim;
		const auto& decoder = engine->triggerCentral.triggerState;

		// Spin up and run for a while without phase info
		sim.setRpm(testRpm);
		sim.runRevolutions(7);

		ASSERT_TRUE(decoder.expectDisambiguation());
		ASSERT_FALSE(decoder.hasSynchronizedPhase());
		ASSERT_TRUE(noTriggerErrors(sim));
		ASSERT_NEAR(testRpm, Sensor::getOrZero(SensorType::Rpm), 1);

		if (m_scenario == Scenario::SimultaneousToSeq) {
			// Get phase early, then crank for a while before ending cranking
			fitCam(camToothAfterSync, wrongGuess);
			sim.runRevolutions(6);

			ASSERT_TRUE(decoder.hasSynchronizedPhase());
			ASSERT_TRUE(engine->rpmCalculator.isCranking());
			ASSERT_EQ(IM_SIMULTANEOUS, getCurrentInjectionMode());
			transitionTime = sim.nowUs();

			// End cranking: next RPM update takes us to running
			engineConfiguration->cranking.rpm = 100;
		} else {
			ASSERT_EQ(IM_BATCH, getCurrentInjectionMode());
			transitionTime = sim.nowUs();

			// Cam syncs part way through one of the next revolutions
			fitCam(camToothAfterSync, wrongGuess);
		}

		sim.runRevolutions(16);

		ASSERT_TRUE(decoder.hasSynchronizedPhase());
		phaseShift = decoder.m_phaseAdjustment;

		ASSERT_EQ(IM_SEQUENTIAL, getCurrentInjectionMode());
		ASSERT_EQ(m_scenario == Scenario::BatchToSeqCranking, engine->rpmCalculator.isCranking());
		ASSERT_TRUE(noTriggerErrors(sim));

		// Stop, let the last pulses finish, then make sure nothing is stuck open
		sim.setRpm(0);
		sim.runSeconds(1);

		EXPECT_TRUE(allOutputsOff());
		for (size_t i = 0; i < 4; i++) {
			EXPECT_EQ(0, enginePins.injectors[i].getOverlappingCounter()) << "injector " << i;
		}
	}

	// Fuel (injector open time, in microseconds) received by each injector in each intake window,
	// from two cycles before its first sequential pulse to three cycles after
	std::vector<std::vector<float>> fuelPerWindow(float& fullPulse) const {
		std::vector<std::vector<float>> result;
		fullPulse = 0;

		for (int inj = 0; inj < 4; inj++) {
			// What this injector really did, as seen at its pin
			auto pulses = recorder.pulses(OutputKind::Injector, inj);

			// The last two pulses are steady-state sequential: they give the size of a full cycle of
			// fuel, and the length of an engine cycle
			EXPECT_GE(pulses.size(), 2u) << "injector " << inj << " hardly fired";
			if (pulses.size() < 2) {
				result.push_back({});
				continue;
			}

			const auto& lastSeq = pulses[pulses.size() - 1];
			const auto& prevSeq = pulses[pulses.size() - 2];

			fullPulse = lastSeq.endUs - lastSeq.startUs;
			int64_t cycle = lastSeq.endUs - prevSeq.endUs;

			// First full-size pulse after the transition is the first sequential one: non-sequential
			// pulses are a fraction of the size
			const OutputPulse* firstSeq = nullptr;
			for (const auto& p : pulses) {
				if (p.startUs > transitionTime &&
					std::abs((float)(p.endUs - p.startUs) - fullPulse) < 0.02f * fullPulse) {
					firstSeq = &p;
					break;
				}
			}
			EXPECT_NE(firstSeq, nullptr) << "injector " << inj << " never went sequential";
			if (!firstSeq) {
				result.push_back({});
				continue;
			}

			// Window boundaries 90 degrees after this injector's own end of injection, so no pulse
			// lands on a boundary
			int64_t anchor = firstSeq->endUs + cycle / 8;

			std::vector<float> windows;
			for (int k = -2; k <= 3; k++) {
				int64_t windowStart = anchor + (k - 1) * cycle;
				int64_t windowEnd = anchor + k * cycle;

				float fuel = 0;
				for (const auto& p : pulses) {
					if (p.endUs > windowStart && p.endUs <= windowEnd) {
						fuel += p.endUs - p.startUs;
					}
				}
				windows.push_back(fuel);
			}

			result.push_back(windows);
		}

		return result;
	}

	EngineTestHelper eth{engine_type_e::TEST_ENGINE};
	OutputRecorder recorder;
	angle_t phaseShift = -1;
	int64_t transitionTime = 0;

private:
	Scenario m_scenario;
	std::unique_ptr<EngineSim> m_sim;
};

std::vector<std::vector<float>> runTransition(Scenario scenario, float camToothAfterSync, bool wrongGuess) {
	TransitionSim sim(scenario);
	sim.run(camToothAfterSync, wrongGuess);

	EXPECT_FLOAT_EQ(wrongGuess ? 360 : 0, sim.phaseShift) << "phase guess wasn't the case under test";

	float fullPulse;
	auto windows = sim.fuelPerWindow(fullPulse);
	EXPECT_GT(fullPulse, 0);

	// Every cylinder gets exactly one cycle of fuel per intake, before, during and after the switch
	for (size_t inj = 0; inj < windows.size(); inj++) {
		for (size_t k = 0; k < windows[inj].size(); k++) {
			EXPECT_NEAR(fullPulse, windows[inj][k], 0.02f * fullPulse)
					<< "injector " << inj << " window " << (int)k - 2 << " (0 = first sequential intake)"
					<< (wrongGuess ? ", wrong phase guess" : ", right phase guess") << ", cam tooth "
					<< camToothAfterSync << " deg after crank sync";
		}
	}

	return windows;
}

void checkRightAndWrongGuessMatch(Scenario scenario) {
	// Vary where in the cycle phase sync lands relative to the injection events
	for (float camToothAfterSync : {37, 127, 217, 307}) {
		auto right = runTransition(scenario, camToothAfterSync, /*wrongGuess=*/false);
		auto wrong = runTransition(scenario, camToothAfterSync, /*wrongGuess=*/true);

		ASSERT_EQ(right.size(), wrong.size());
		for (size_t inj = 0; inj < right.size(); inj++) {
			ASSERT_EQ(right[inj].size(), wrong[inj].size());
			for (size_t k = 0; k < right[inj].size(); k++) {
				EXPECT_NEAR(right[inj][k], wrong[inj][k], 0.02f * right[inj][k])
						<< "injector " << inj << " window " << (int)k - 2 << ", cam tooth " << camToothAfterSync
						<< " deg after crank sync";
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
