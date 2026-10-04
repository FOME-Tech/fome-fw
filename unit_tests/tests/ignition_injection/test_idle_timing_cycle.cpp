#include "pch.h"

#include "idle_thread.h"
#include "spark_logic.h"
#include <cmath>

using ::testing::_;

namespace {
struct IdleTimingResult {
	double advance[2]{};
	int sparks[2]{};
	int overdwell = 0;
	float maxCorrection = 0;
	float minRpm = 10000;
	float maxRpm = 0;
};

// Constant 100 ms engine cycles with cyclic speed ripple, independently of ECU RPM estimates.
IdleTimingResult runIdleTimingRipple(int fastPhaseUs, double ripplePhase, bool useCycleRpm = false) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	EXPECT_FALSE(engineConfiguration->idleTimingUseCycleRpm);
	engineConfiguration->idleTimingUseCycleRpm = useCycleRpm;
	setCylinderCount(2);
	engineConfiguration->timing_offset_cylinder[1] = -60;
	engineConfiguration->ignitionMode = IM_INDIVIDUAL_COILS;
	engineConfiguration->isIgnitionEnabled = true;
	engineConfiguration->isInjectionEnabled = false;
	engineConfiguration->useIdleTimingPidControl = true;
	engineConfiguration->idleTimingPid = {};
	engineConfiguration->idleTimingPid.pFactor = 0.1142f;
	engineConfiguration->idleTimingPid.dFactor = 0.03f;
	engineConfiguration->idleTimingPid.minValue = -6;
	engineConfiguration->idleTimingPid.maxValue = 12;
	engineConfiguration->useSeparateAdvanceForIdle = false;
	engineConfiguration->globalTriggerAngleOffset = 0;
	engineConfiguration->skippedWheelOnCam = true;
	engineConfiguration->trigger.customTotalToothCount = 72;
	engineConfiguration->trigger.customSkippedToothCount = 0;
	setTable(config->ignitionTable, 10);
	setTable(config->ignitionIatCorrTable, 0);
	setArrayValues(config->cltTimingExtra, 0);
	setArrayValues(config->sparkDwellValues, 2.86f);
	setArrayValues(config->dwellVoltageCorrValues, 1);
	for (auto& trim : config->ignTrims) {
		setTable(trim.table, 0);
	}
	eth.setTriggerType(trigger_type_e::TT_TOOTHED_WHEEL);
	prepareOutputSignals();
	Sensor::setMockValue(SensorType::DriverThrottleIntent, 0);
	Sensor::setMockValue(SensorType::BatteryVoltage, 13.5f);
	MockIdleTargetController target;
	IIdleTargetController::Output idle;
	idle.target = {1200, 1500, 1650};
	idle.phase = IIdleController::Phase::Idling;
	idle.crankingTaperFraction = 1;
	EXPECT_CALL(target, getOutput(_)).WillRepeatedly(Return(idle));
	engine->engineModules.get<IdleTargetController>().set(&target);
	engine->module<IdleController>().unmock().init();

	constexpr double pi = 3.14159265358979323846;
	auto timeAtAngle = [=](double angle) {
		double theta = 2 * pi * (angle + ripplePhase) / 720;
		return 1000000 + 100000 * (angle / 720 + 0.025 * std::sin(theta) - 0.008 * std::sin(2 * theta));
	};
	auto angleAtTime = [&](double time) {
		double lo = (time - 1000000) * 720 / 100000 - 40;
		double hi = lo + 80;
		for (int i = 0; i < 40; i++) {
			double mid = (lo + hi) / 2;
			if (timeAtAngle(mid) < time) {
				lo = mid;
			} else {
				hi = mid;
			}
		}
		return (lo + hi) / 2;
	};
	IdleTimingResult result;
	engine->onIgnitionEvent = [&](IgnitionContext ctx, bool charging) {
		double angle = angleAtTime(getTimeNowUs());
		if (charging || angle < 7200 || angle >= 36000) {
			return;
		}
		int cylinder = ctx.eventIndex;
		result.advance[cylinder] += std::remainder(engine->cylinders[cylinder].getAngleOffset() - angle, 720.0);
		result.sparks[cylinder]++;
		result.overdwell += ctx.isOverdwellProtect;
	};
	int nextFast = std::lround(timeAtAngle(0)) + fastPhaseUs;
	for (int tooth = 0; tooth <= 72 * 50; tooth++) {
		int toothTime = std::lround(timeAtAngle(tooth * 10));
		while (nextFast <= toothTime) {
			eth.setTimeAndInvokeEventsUs(nextFast);
			engine->periodicFastCallback();
			if (tooth >= 72 * 10) {
				result.maxCorrection =
						std::max(result.maxCorrection, std::abs(float(engine->ignitionState.timingPidCorrection)));
			}
			nextFast += 4000;
		}
		eth.setTimeAndInvokeEventsUs(toothTime);
		eth.firePrimaryTriggerRise();
		if (tooth >= 72 * 10) {
			float rpm = Sensor::getOrZero(SensorType::Rpm);
			result.minRpm = std::min(result.minRpm, rpm);
			result.maxRpm = std::max(result.maxRpm, rpm);
		}
	}
	engine->onIgnitionEvent = {};
	engine->engineModules.get<IdleTargetController>().set(nullptr);
	for (int cylinder = 0; cylinder < 2; cylinder++) {
		EXPECT_EQ(result.sparks[cylinder], 40);
		result.advance[cylinder] /= result.sparks[cylinder];
	}
	return result;
}
} // namespace

TEST(idleTiming, constantCycleSpeedRejectsCyclicRipple) {
	for (double ripplePhase : {0.0, 360.0}) {
		for (int fastPhaseUs : {0, 1000, 2000, 3000}) {
			SCOPED_TRACE(::testing::Message() << "ripple phase=" << ripplePhase << ", fast phase=" << fastPhaseUs);
			auto result = runIdleTimingRipple(fastPhaseUs, ripplePhase, true);
			// The normal RPM sensor must still follow the tooth-speed ripple.
			EXPECT_GT(result.maxRpm - result.minRpm, 300);
			EXPECT_NEAR(result.maxCorrection, 0, 0.02);
			EXPECT_NEAR(result.advance[0], 10, 1);
			EXPECT_NEAR(result.advance[1], 10, 1);
			EXPECT_NEAR(result.advance[0] - result.advance[1], 0, 1);
			EXPECT_EQ(result.overdwell, 0);
			printf("IDLE_TIMING fast=%d phi=%.0f advance=%.4f/%.4f diff=%.4f maxCorrection=%.4f rpmRange=%.1f/%.1f\n",
				   fastPhaseUs,
				   ripplePhase,
				   result.advance[0],
				   result.advance[1],
				   result.advance[0] - result.advance[1],
				   result.maxCorrection,
				   result.minRpm,
				   result.maxRpm);
		}
	}
}

TEST(idleTiming, instantaneousFeedbackRemainsDefault) {
	// The unselected mode retains the fast feedback, including the reproduced ripple response.
	auto result = runIdleTimingRipple(0, 360);
	EXPECT_GT(result.maxRpm - result.minRpm, 300);
	EXPECT_NEAR(result.maxCorrection, 12, 0.02);
	EXPECT_GT(std::abs(result.advance[0] - result.advance[1]), 15);
	EXPECT_EQ(result.overdwell, 0);
}

TEST(idleTiming, cycleMeasurementTracksSpeedChangesAndRestart) {
	// Automatic instant RPM, explicit instant RPM, and the legacy cycle-RPM sensor.
	for (auto [teeth, forcedInstant] : {std::pair{24, false}, std::pair{4, true}, std::pair{4, false}}) {
		SCOPED_TRACE(::testing::Message() << "teeth=" << teeth << ", forced instant=" << forcedInstant);
		EngineTestHelper eth(engine_type_e::TEST_ENGINE);
		engineConfiguration->isIgnitionEnabled = false;
		engineConfiguration->isInjectionEnabled = false;
		engineConfiguration->skippedWheelOnCam = true;
		engineConfiguration->alwaysInstantRpm = forcedInstant;
		engineConfiguration->trigger.customTotalToothCount = teeth;
		engineConfiguration->trigger.customSkippedToothCount = 0;
		eth.setTriggerType(trigger_type_e::TT_TOOTHED_WHEEL);
		auto& rpm = engine->rpmCalculator;
		EXPECT_EQ(rpm.getCycleRpm().rpm, 0);
		eth.smartFireRise(120.0f / teeth);
		EXPECT_EQ(rpm.getCycleRpm().rpm, 0);
		for (int tooth = 0; tooth < teeth * 4; tooth++) {
			eth.smartFireRise(120.0f / teeth);
		}
		EXPECT_NEAR(rpm.getCycleRpm().rpm, 1000, 0.01);
		EXPECT_NEAR(rpm.getCycleRpm().rpmRate, 0, 0.01);

		MockIdleTargetController target;
		IIdleTargetController::Output idle;
		idle.target = {1000, 1500, 1650};
		idle.phase = IIdleController::Phase::Idling;
		idle.crankingTaperFraction = 1;
		EXPECT_CALL(target, getOutput(_)).WillRepeatedly(Return(idle));
		engine->engineModules.get<IdleTargetController>().set(&target);
		engineConfiguration->useIdleTimingPidControl = true;
		engineConfiguration->idleTimingPid = {};
		engineConfiguration->idleTimingPid.pFactor = 0.01f;
		engineConfiguration->idleTimingPid.dFactor = 0.003f;
		engineConfiguration->idleTimingPid.minValue = -30;
		engineConfiguration->idleTimingPid.maxValue = 30;
		engineConfiguration->idleTimingUseCycleRpm = true;
		auto& controller = engine->module<IdleController>().unmock();
		controller.init();
		controller.getIdlePosition(1000, 0);
		engine->ignitionState.updateAdvanceCorrections(50);
		EXPECT_EQ(float(engine->ignitionState.timingPidCorrection), 0);

		// Faster teeth alone must not advance the cycle measurement or timing PID.
		for (int tooth = 0; tooth < teeth - 1; tooth++) {
			eth.smartFireRise(96.0f / teeth);
		}
		EXPECT_NEAR(rpm.getCycleRpm().rpm, 1000, 0.01);
		EXPECT_NEAR(Sensor::getOrZero(SensorType::Rpm), teeth >= 24 || forcedInstant ? 1250 : 1000, 0.01);
		engine->ignitionState.updateAdvanceCorrections(50);
		EXPECT_EQ(float(engine->ignitionState.timingPidCorrection), 0);

		// Switching to Instantaneous must select both original inputs, even with a legacy cycle RPM sensor.
		auto previousConfiguration = *engineConfiguration;
		engineConfiguration->idleTimingUseCycleRpm = false;
		controller.onConfigurationChange(&previousConfiguration);
		rpm.rpmRate = 100;
		engine->ignitionState.updateAdvanceCorrections(50);
		EXPECT_NEAR(float(engine->ignitionState.timingPidCorrection), -2.5f - 0.003f * 100, 0.02);
		previousConfiguration = *engineConfiguration;
		engineConfiguration->idleTimingUseCycleRpm = true;
		controller.onConfigurationChange(&previousConfiguration);
		engine->ignitionState.updateAdvanceCorrections(50);
		EXPECT_EQ(float(engine->ignitionState.timingPidCorrection), 0);

		eth.smartFireRise(96.0f / teeth);
		EXPECT_NEAR(rpm.getCycleRpm().rpm, 1250, 0.01);
		EXPECT_NEAR(rpm.getCycleRpm().rpmRate, 250 / 0.096f, 0.01);
		engine->ignitionState.updateAdvanceCorrections(50);
		// Both P and D consume the completed cycle, with no extra 2x divisor in D.
		EXPECT_NEAR(float(engine->ignitionState.timingPidCorrection), -2.5f - 0.003f * 250 / 0.096f, 0.02);
		for (int tooth = 0; tooth < teeth; tooth++) {
			eth.smartFireRise(96.0f / teeth);
		}
		EXPECT_NEAR(rpm.getCycleRpm().rpmRate, 0, 0.01);
		engine->ignitionState.updateAdvanceCorrections(50);
		EXPECT_NEAR(float(engine->ignitionState.timingPidCorrection), -2.5f, 0.02);

		rpm.setStopSpinning();
		controller.onEngineStop();
		EXPECT_EQ(rpm.getCycleRpm().rpm, 0);
		EXPECT_EQ(rpm.getCycleRpm().rpmRate, 0);
		engine->ignitionState.updateAdvanceCorrections(50);
		EXPECT_EQ(float(engine->ignitionState.timingPidCorrection), 0);
		// The first cycle after restart must not include time spent stopped.
		eth.moveTimeForwardMs(500);
		for (int tooth = 0; tooth < teeth; tooth++) {
			eth.smartFireRise(120.0f / teeth);
		}
		EXPECT_EQ(rpm.getCycleRpm().rpm, 0);
		for (int tooth = 0; tooth < teeth; tooth++) {
			eth.smartFireRise(120.0f / teeth);
		}
		EXPECT_NEAR(rpm.getCycleRpm().rpm, 1000, 0.01);
		EXPECT_EQ(rpm.getCycleRpm().rpmRate, 0);
		engine->engineModules.get<IdleTargetController>().set(nullptr);
	}
}
