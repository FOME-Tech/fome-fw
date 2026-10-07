#include "pch.h"

#include "idle_thread.h"
#include <cmath>

using ::testing::_;

namespace {
void configureRollingTrigger(EngineTestHelper& eth, int teeth = 24, bool isCam = true, bool twoStroke = false) {
	engineConfiguration->isIgnitionEnabled = false;
	engineConfiguration->isInjectionEnabled = false;
	engineConfiguration->idleTimingUseRollingRpm = true;
	engineConfiguration->skippedWheelOnCam = isCam;
	engineConfiguration->twoStroke = twoStroke;
	engineConfiguration->trigger.customTotalToothCount = teeth;
	engineConfiguration->trigger.customSkippedToothCount = 0;
	eth.setTriggerType(trigger_type_e::TT_TOOTHED_WHEEL);
}

struct Response {
	int firstUs = -1;
	int halfUs = -1;
	int settledUs = -1;
};

Response runSpeedDrop(int toothPhase, int fastPhaseUs) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureRollingTrigger(eth);
	eth.smartFireRise(5);
	for (int i = 0; i < 24 * 4 + toothPhase; i++) {
		eth.smartFireRise(5);
	}

	MockIdleTargetController target;
	IIdleTargetController::Output idle;
	idle.target = {1000, 1500, 1650};
	idle.phase = IIdleController::Phase::Idling;
	idle.crankingTaperFraction = 1;
	EXPECT_CALL(target, getOutput(_)).WillRepeatedly(Return(idle));
	engine->engineModules.get<IdleTargetController>().set(&target);
	Sensor::setMockValue(SensorType::DriverThrottleIntent, 0);
	Sensor::setMockValue(SensorType::BatteryVoltage, 13.5f);
	engineConfiguration->useIdleTimingPidControl = true;
	engineConfiguration->idleTimingPid = {};
	engineConfiguration->idleTimingPid.pFactor = 0.01f;
	engineConfiguration->idleTimingPid.minValue = -30;
	engineConfiguration->idleTimingPid.maxValue = 30;
	engine->module<IdleController>().unmock().init();

	int startUs = getTimeNowUs();
	int nextFast = startUs + fastPhaseUs;
	Response result;
	// 1000 -> 800 RPM: every 30-degree tooth now takes 6.25 ms instead of 5 ms.
	for (int i = 1; i <= 24 * 3; i++) {
		int nextTooth = startUs + i * 6250;
		while (nextFast < nextTooth) {
			eth.setTimeAndInvokeEventsUs(nextFast);
			engine->periodicFastCallback();
			float correction = engine->ignitionState.timingPidCorrection;
			int delayUs = nextFast - startUs;
			if (correction > 0.01f && result.firstUs < 0) {
				result.firstUs = delayUs;
			}
			if (correction >= 1 && result.halfUs < 0) {
				result.halfUs = delayUs;
			}
			if (correction >= 1.99f && result.settledUs < 0) {
				result.settledUs = delayUs;
			}
			nextFast += 4000;
		}
		eth.setTimeAndInvokeEventsUs(nextTooth);
		eth.firePrimaryTriggerRise();
	}
	engine->engineModules.get<IdleTargetController>().set(nullptr);
	return result;
}
} // namespace

TEST(idleTimingRolling, loadResponseDoesNotWaitForCycleBoundary) {
	for (int toothPhase : {0, 6, 12, 18}) {
		for (int fastPhaseUs : {0, 1000, 2000, 3000}) {
			SCOPED_TRACE(::testing::Message() << "tooth phase=" << toothPhase << ", fast phase=" << fastPhaseUs);
			auto rolling = runSpeedDrop(toothPhase, fastPhaseUs);
			EXPECT_GE(rolling.firstUs, 6250);
			EXPECT_LT(rolling.firstUs, 10250);
			EXPECT_LT(rolling.firstUs, (24 - toothPhase) * 6250);
			EXPECT_GE(rolling.halfUs, 62500);
			EXPECT_LT(rolling.halfUs, 80000);
			EXPECT_GE(rolling.settledUs, 150000);
			EXPECT_LT(rolling.settledUs, 154000);
			printf("ROLLING_RESPONSE tooth=%d fast=%d first=%d half=%d settled=%d us\n",
				   toothPhase,
				   fastPhaseUs,
				   rolling.firstUs,
				   rolling.halfUs,
				   rolling.settledUs);
		}
	}
}

TEST(idleTimingRolling, proportionalAndDerivativeInputsFollowRollingWindow) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureRollingTrigger(eth);
	eth.smartFireRise(5);
	for (int i = 0; i < 24 * 4; i++) {
		eth.smartFireRise(5);
	}
	auto& rpm = engine->rpmCalculator;
	EXPECT_NEAR(rpm.getRollingCycleRpm().rpm, 1000, 0.01);
	EXPECT_NEAR(rpm.getRollingCycleRpm().rpmRate, 0, 0.01);
	eth.smartFireRise(6.25f);
	auto rolling = rpm.getRollingCycleRpm();
	float expectedRpm = 120000.0f / 121.25f;
	float expectedRate = (expectedRpm - 1000) / 0.00625f;
	EXPECT_NEAR(rolling.rpm, expectedRpm, 0.01);
	EXPECT_NEAR(rolling.rpmRate, expectedRate, 0.1);

	MockIdleTargetController target;
	IIdleTargetController::Output idle;
	idle.target = {1000, 1500, 1650};
	idle.phase = IIdleController::Phase::Idling;
	EXPECT_CALL(target, getOutput(_)).WillRepeatedly(Return(idle));
	engine->engineModules.get<IdleTargetController>().set(&target);
	engineConfiguration->useIdleTimingPidControl = true;
	engineConfiguration->idleTimingPid = {};
	engineConfiguration->idleTimingPid.pFactor = 0.01f;
	engineConfiguration->idleTimingPid.dFactor = 0.003f;
	engineConfiguration->idleTimingPid.minValue = -30;
	engineConfiguration->idleTimingPid.maxValue = 30;
	auto& controller = engine->module<IdleController>().unmock();
	controller.init();
	controller.getIdlePosition(1000, 0);
	engine->ignitionState.updateAdvanceCorrections(50);
	EXPECT_NEAR(
			float(engine->ignitionState.timingPidCorrection),
			0.01f * (1000 - expectedRpm) - 0.003f * expectedRate,
			0.02);
	for (int i = 0; i < 24; i++) {
		eth.smartFireRise(6.25f);
	}
	EXPECT_NEAR(rpm.getRollingCycleRpm().rpm, 800, 0.01);
	EXPECT_NEAR(rpm.getRollingCycleRpm().rpmRate, 0, 0.01);
	engine->ignitionState.updateAdvanceCorrections(50);
	EXPECT_NEAR(float(engine->ignitionState.timingPidCorrection), 2, 0.02);
	engine->engineModules.get<IdleTargetController>().set(nullptr);
}

TEST(idleTimingRolling, cycleLengthAndTimestampWrap) {
	for (auto [isCam, twoStroke] : {std::pair{true, false}, std::pair{false, false}, std::pair{false, true}}) {
		SCOPED_TRACE(::testing::Message() << "cam=" << isCam << ", two stroke=" << twoStroke);
		EngineTestHelper eth(engine_type_e::TEST_ENGINE);
		configureRollingTrigger(eth, 24, isCam, twoStroke);
		// Cross the 32-bit native-tick boundary using real decoded timestamps.
		int wrapUs = (uint64_t{1} << 32) / US_TO_NT_MULTIPLIER;
		eth.setTimeAndInvokeEventsUs(wrapUs - 300000);
		for (int i = 0; i < 24 * 8; i++) {
			eth.smartFireRise(5);
		}
		EXPECT_NEAR(engine->rpmCalculator.getRollingCycleRpm().rpm, isCam ? 1000 : 500, 0.01);
		EXPECT_NEAR(engine->rpmCalculator.getRollingCycleRpm().rpmRate, 0, 0.01);
	}
}

TEST(idleTimingRolling, stopAndTriggerChangeInvalidateHistory) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureRollingTrigger(eth);
	for (int i = 0; i < 24 * 4 + 1; i++) {
		eth.smartFireRise(5);
	}
	auto& rpm = engine->rpmCalculator;
	ASSERT_NEAR(rpm.getRollingCycleRpm().rpm, 1000, 0.01);
	engine->OnTriggerSynchronizationLost();
	EXPECT_EQ(rpm.getRollingCycleRpm().rpm, 0);
	EXPECT_EQ(rpm.getRollingCycleRpm().rpmRate, 0);
	eth.moveTimeForwardMs(500);
	for (int i = 0; i < 24; i++) {
		eth.smartFireRise(5);
		EXPECT_EQ(rpm.getRollingCycleRpm().rpm, 0);
	}
	eth.smartFireRise(5);
	EXPECT_NEAR(rpm.getRollingCycleRpm().rpm, 1000, 0.01);
	EXPECT_NEAR(rpm.getRollingCycleRpm().rpmRate, 0, 0.01);

	configureRollingTrigger(eth, 12);
	EXPECT_EQ(rpm.getRollingCycleRpm().rpm, 0);
	for (int i = 0; i < 12; i++) {
		eth.smartFireRise(10);
		EXPECT_EQ(rpm.getRollingCycleRpm().rpm, 0);
	}
	for (int i = 0; i < 12 * 3; i++) {
		eth.smartFireRise(10);
	}
	EXPECT_NEAR(rpm.getRollingCycleRpm().rpm, 1000, 0.01);
	EXPECT_NEAR(rpm.getRollingCycleRpm().rpmRate, 0, 0.01);
}

TEST(idleTimingRolling, camPhaseChangeRearmsWindow) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureRollingTrigger(eth, 24, false);
	for (int i = 0; i < 24 * 8 + 1; i++) {
		eth.smartFireRise(5);
	}
	auto& tc = engine->triggerCentral;
	ASSERT_NEAR(engine->rpmCalculator.getRollingCycleRpm().rpm, 500, 0.01);
	int remainder = (tc.triggerState.getCrankSynchronizationCounter() % 2) ^ 1;
	tc.triggerState.syncEnginePhase(2, remainder, 720);
	eth.smartFireRise(5);
	EXPECT_EQ(engine->rpmCalculator.getRollingCycleRpm().rpm, 0);
	EXPECT_EQ(engine->rpmCalculator.getRollingCycleRpm().rpmRate, 0);
	for (int i = 0; i < 24 * 2; i++) {
		eth.smartFireRise(5);
	}
	EXPECT_NEAR(engine->rpmCalculator.getRollingCycleRpm().rpm, 500, 0.01);
}

TEST(idleTimingRolling, missingTeethRejectRippleAtEveryDecodedEvent) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureRollingTrigger(eth, 36, false);
	engineConfiguration->trigger.customSkippedToothCount = 2;
	eth.setTriggerType(trigger_type_e::TT_TOOTHED_WHEEL);
	float minRpm = 10000;
	float maxRpm = 0;
	constexpr double pi = 3.14159265358979323846;
	for (int revolution = 0; revolution < 16; revolution++) {
		for (int tooth = 0; tooth < 34; tooth++) {
			double angle = revolution * 360 + tooth * 10;
			double theta = 2 * pi * angle / 720;
			int timeUs = std::lround(
					1000000 + 72000 * (angle / 720 + 0.025 * std::sin(theta) - 0.008 * std::sin(2 * theta)));
			eth.setTimeAndInvokeEventsUs(timeUs);
			eth.firePrimaryTriggerRise();
			if (revolution >= 8) {
				auto rolling = engine->rpmCalculator.getRollingCycleRpm();
				EXPECT_NEAR(rolling.rpm, 120000.0f / 72, 0.02);
				EXPECT_NEAR(rolling.rpmRate, 0, 0.05);
				float rpm = Sensor::getOrZero(SensorType::Rpm);
				minRpm = std::min(minRpm, rpm);
				maxRpm = std::max(maxRpm, rpm);
			}
		}
	}
	EXPECT_GT(maxRpm - minRpm, 300);
}

TEST(idleTimingRolling, bothEdgesWithUnequalToothWidths) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureRollingTrigger(eth);
	eth.setTriggerType(trigger_type_e::TT_ONE);
	ASSERT_FALSE(engine->triggerCentral.triggerShape.useOnlyRisingEdges);
	for (int i = 0; i < 16; i++) {
		eth.smartFireRise(40);
		if (i >= 4) {
			EXPECT_NEAR(engine->rpmCalculator.getRollingCycleRpm().rpm, 2000, 0.01);
			EXPECT_NEAR(engine->rpmCalculator.getRollingCycleRpm().rpmRate, 0, 0.01);
		}
		eth.smartFireFall(20);
		if (i >= 4) {
			EXPECT_NEAR(engine->rpmCalculator.getRollingCycleRpm().rpm, 2000, 0.01);
			EXPECT_NEAR(engine->rpmCalculator.getRollingCycleRpm().rpmRate, 0, 0.01);
		}
	}
}

TEST(idleTimingRolling, modeChangeRearmsWindowAndPreservesLegacyInputs) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	EXPECT_FALSE(engineConfiguration->idleTimingUseRollingRpm);
	configureRollingTrigger(eth);
	for (int i = 0; i < 24 * 4 + 1; i++) {
		eth.smartFireRise(5);
	}
	MockIdleTargetController target;
	IIdleTargetController::Output idle;
	idle.target = {1000, 1500, 1650};
	idle.phase = IIdleController::Phase::Idling;
	EXPECT_CALL(target, getOutput(_)).WillRepeatedly(Return(idle));
	engine->engineModules.get<IdleTargetController>().set(&target);
	engineConfiguration->useIdleTimingPidControl = true;
	engineConfiguration->idleTimingPid = {};
	engineConfiguration->idleTimingPid.pFactor = 0.01f;
	engineConfiguration->idleTimingPid.dFactor = 0.003f;
	engineConfiguration->idleTimingPid.minValue = -30;
	engineConfiguration->idleTimingPid.maxValue = 30;
	auto& controller = engine->module<IdleController>().unmock();
	controller.init();
	controller.getIdlePosition(1000, 0);
	engine->rpmCalculator.rpmRate = -100;
	auto previousConfiguration = *engineConfiguration;
	engineConfiguration->idleTimingUseRollingRpm = false;
	controller.onConfigurationChange(&previousConfiguration);
	EXPECT_EQ(engine->rpmCalculator.getRollingCycleRpm().rpm, 0);
	engine->ignitionState.updateAdvanceCorrections(50);
	EXPECT_NEAR(float(engine->ignitionState.timingPidCorrection), 0.3f, 0.02);
	previousConfiguration = *engineConfiguration;
	engineConfiguration->idleTimingUseRollingRpm = true;
	controller.onConfigurationChange(&previousConfiguration);
	for (int i = 0; i < 24; i++) {
		eth.smartFireRise(5);
		engine->ignitionState.updateAdvanceCorrections(50);
		EXPECT_EQ(float(engine->ignitionState.timingPidCorrection), 0);
	}
	eth.smartFireRise(5);
	EXPECT_NEAR(engine->rpmCalculator.getRollingCycleRpm().rpm, 1000, 0.01);
	engine->engineModules.get<IdleTargetController>().set(nullptr);
}

TEST(idleTimingRolling, recordedOddFireCycleRejectsRipple) {
	// Primary rising timestamps from 2026-09-18_16.52.52.csv, one 36-2/720-degree
	// cycle at 433.1 RPM. Repeating the recorded cycle isolates phase ripple from
	// cycle-to-cycle drift; this is a measured shaft profile, not a combustion model.
	constexpr uint32_t recordedUs[] = {
			0,		2331,	4641,	6934,	9227,	11557,	13895,	16234,	18573,	20946,	23345,	25812,
			28311,	30852,	33448,	36171,	39019,	42139,	45551,	49695,	56122,	63919,	70089,	77173,
			83216,	88599,	92395,	95355,	97875,	100215, 102503, 104761, 107001, 109250, 116481, 119035,
			121742, 124614, 127659, 130945, 134421, 138156, 142126, 146427, 150955, 155792, 160743, 165891,
			170899, 175754, 180298, 184986, 189574, 194314, 199140, 204220, 209314, 214219, 219105, 224617,
			229966, 235780, 242254, 249135, 256101, 261987, 266190, 269380, 277050,
	};
	static_assert(std::size(recordedUs) == 69);
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureRollingTrigger(eth, 36, false);
	engineConfiguration->trigger.customSkippedToothCount = 2;
	eth.setTriggerType(trigger_type_e::TT_TOOTHED_WHEEL);
	float minimum = 10000;
	float maximum = 0;
	float expected = 120000000.0f / recordedUs[68];
	for (int cycle = 0; cycle < 12; cycle++) {
		for (int tooth = 0; tooth < 68; tooth++) {
			eth.setTimeAndInvokeEventsUs(1000000 + cycle * recordedUs[68] + recordedUs[tooth]);
			eth.firePrimaryTriggerRise();
			if (cycle >= 6) {
				auto rolling = engine->rpmCalculator.getRollingCycleRpm();
				EXPECT_NEAR(rolling.rpm, expected, 0.01);
				EXPECT_NEAR(rolling.rpmRate, 0, 0.01);
				float instantaneous = engine->triggerCentral.instantRpm.getInstantRpm();
				minimum = std::min(minimum, instantaneous);
				maximum = std::max(maximum, instantaneous);
			}
		}
	}
	EXPECT_GT(maximum - minimum, 400);
}

TEST(idleTimingRolling, repeatedReadsAndCoincidentEventsPreserveDerivative) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureRollingTrigger(eth);
	auto& rpm = engine->rpmCalculator;
	auto& timestamps = engine->triggerCentral.instantRpm.timeOfLastEvent;
	EnginePhaseInfo phase{};
	phase.timestamp = efitick_t{US2NT(1000000)};
	uint32_t event = 0;
	auto feed = [&](int deltaUs) {
		phase.timestamp += US2NT(deltaUs);
		uint32_t index = (event++ % 24) * 2;
		rpm.updateRollingCycleRpm(index, phase);
		timestamps[index] = phase.timestamp;
	};
	for (int i = 0; i < 72; i++) {
		feed(5000);
	}
	feed(6250);
	auto previous = rpm.getRollingCycleRpm();
	EXPECT_LT(previous.rpmRate, -1000);
	for (int i = 0; i < 8; i++) {
		auto repeated = rpm.getRollingCycleRpm();
		EXPECT_EQ(repeated.rpm, previous.rpm);
		EXPECT_EQ(repeated.rpmRate, previous.rpmRate);
	}
	feed(0);
	auto coincident = rpm.getRollingCycleRpm();
	EXPECT_GT(coincident.rpm, previous.rpm);
	EXPECT_EQ(coincident.rpmRate, previous.rpmRate);
	// A too-short cycle is rejected and requires a fresh complete traversal.
	uint32_t index = (event % 24) * 2;
	phase.timestamp += US2NT(5000);
	timestamps[index] = phase.timestamp - 1;
	rpm.updateRollingCycleRpm(index, phase);
	timestamps[index] = phase.timestamp;
	event++;
	EXPECT_EQ(rpm.getRollingCycleRpm().rpm, 0);
	for (int i = 0; i < 24; i++) {
		feed(5000);
		EXPECT_EQ(rpm.getRollingCycleRpm().rpm, 0);
	}
	feed(5000);
	EXPECT_NEAR(rpm.getRollingCycleRpm().rpm, 1000, 0.01);
	EXPECT_EQ(rpm.getRollingCycleRpm().rpmRate, 0);
}

TEST(idleTimingRolling, ignitionFeedbackIsIndependentOfAirFeedback) {
	for (bool timingRolling : {false, true}) {
		for (bool airRolling : {false, true}) {
			SCOPED_TRACE(::testing::Message() << "timing=" << timingRolling << ", air=" << airRolling);
			EngineTestHelper eth(engine_type_e::TEST_ENGINE);
			EXPECT_FALSE(engineConfiguration->idleAirUseRollingRpm);
			configureRollingTrigger(eth);
			engineConfiguration->idleTimingUseRollingRpm = timingRolling;
			engineConfiguration->idleAirUseRollingRpm = airRolling;
			for (int i = 0; i < 24 * 4 + 1; i++) {
				eth.smartFireRise(5);
			}
			eth.smartFireRise(6.25f);
			auto rolling = engine->rpmCalculator.getRollingCycleRpm();
			if (timingRolling || airRolling) {
				ASSERT_GT(rolling.rpm, 0);
				ASSERT_LT(rolling.rpmRate, 0);
			} else {
				EXPECT_EQ(rolling.rpm, 0);
			}
			MockIdleTargetController target;
			IIdleTargetController::Output idle;
			idle.target = {1000, 1500, 1650};
			idle.phase = IIdleController::Phase::Idling;
			EXPECT_CALL(target, getOutput(_)).WillRepeatedly(Return(idle));
			engine->engineModules.get<IdleTargetController>().set(&target);
			engineConfiguration->useIdleTimingPidControl = true;
			engineConfiguration->idleTimingPid = {};
			engineConfiguration->idleTimingPid.pFactor = 0.01f;
			engineConfiguration->idleTimingPid.dFactor = 0.003f;
			engineConfiguration->idleTimingPid.minValue = -30;
			engineConfiguration->idleTimingPid.maxValue = 30;
			auto& controller = engine->module<IdleController>().unmock();
			controller.init();
			controller.getIdlePosition(1000, 0);
			engine->rpmCalculator.rpmRate = -100;
			float feedbackRpm = timingRolling ? rolling.rpm : engine->triggerCentral.instantRpm.getInstantRpm();
			float feedbackRate = timingRolling ? rolling.rpmRate : engine->rpmCalculator.getRpmAcceleration();
			engine->ignitionState.updateAdvanceCorrections(50);
			EXPECT_NEAR(
					float(engine->ignitionState.timingPidCorrection),
					0.01f * (1000 - feedbackRpm) - 0.003f * feedbackRate,
					0.02);
			engine->engineModules.get<IdleTargetController>().set(nullptr);
		}
	}
}

TEST(idleTimingRolling, changingOneConsumerPreservesTheOtherRollingWindow) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureRollingTrigger(eth);
	for (int i = 0; i < 24 * 4 + 1; i++) {
		eth.smartFireRise(5);
	}
	auto& rpm = engine->rpmCalculator;
	auto& controller = engine->module<IdleController>().unmock();
	auto previous = *engineConfiguration;
	engineConfiguration->idleAirUseRollingRpm = true;
	controller.onConfigurationChange(&previous);
	EXPECT_NEAR(rpm.getRollingCycleRpm().rpm, 1000, 0.01);
	previous = *engineConfiguration;
	engineConfiguration->idleTimingUseRollingRpm = false;
	controller.onConfigurationChange(&previous);
	EXPECT_NEAR(rpm.getRollingCycleRpm().rpm, 1000, 0.01);
	// Air alone must continue updating the measurement at trigger events.
	for (int i = 0; i < 24; i++) {
		eth.smartFireRise(6.25f);
	}
	EXPECT_NEAR(rpm.getRollingCycleRpm().rpm, 800, 0.01);
	previous = *engineConfiguration;
	engineConfiguration->idleTimingUseRollingRpm = true;
	controller.onConfigurationChange(&previous);
	EXPECT_NEAR(rpm.getRollingCycleRpm().rpm, 800, 0.01);
	previous = *engineConfiguration;
	engineConfiguration->idleAirUseRollingRpm = false;
	controller.onConfigurationChange(&previous);
	EXPECT_NEAR(rpm.getRollingCycleRpm().rpm, 800, 0.01);
	previous = *engineConfiguration;
	engineConfiguration->idleTimingUseRollingRpm = false;
	controller.onConfigurationChange(&previous);
	EXPECT_EQ(rpm.getRollingCycleRpm().rpm, 0);
	previous = *engineConfiguration;
	engineConfiguration->idleAirUseRollingRpm = true;
	controller.onConfigurationChange(&previous);
	for (int i = 0; i < 24; i++) {
		eth.smartFireRise(6.25f);
		EXPECT_EQ(rpm.getRollingCycleRpm().rpm, 0);
	}
	eth.smartFireRise(6.25f);
	EXPECT_NEAR(rpm.getRollingCycleRpm().rpm, 800, 0.01);
}
