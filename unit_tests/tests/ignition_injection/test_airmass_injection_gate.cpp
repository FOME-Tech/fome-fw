#include "pch.h"
#include "airmass_injection_state.h"

namespace {
using Status = AirmassInjectionStatus;
using Fault = AirmassInjectionFault;

AirmassInjectionState& gate() {
	return engine->airmassInjectionState;
}

void selectComposite() {
	Sensor::setMockValue(SensorType::Rpm, 0);
	gate().onConfigurationWrite(LM_SD_ALPHA_N, engineConfiguration->fuelAlgorithm != LM_SD_ALPHA_N);
	engineConfiguration->fuelAlgorithm = LM_SD_ALPHA_N;
}

AirmassInjectionState::CalculationToken beginPositiveCalculation() {
	Sensor::setMockValue(SensorType::Rpm, 1000);
	return gate().beginCalculation(engineConfiguration->fuelAlgorithm, 1000, engine->getGlobalConfigurationVersion());
}

void makeReady() {
	auto token = beginPositiveCalculation();
	gate().acceptCalculation();
	gate().completeCalculation(token, true);
	ASSERT_EQ(Status::Ready, gate().status());
}

bool queuePulse(int startUs, int endUs, InjectorContext ctx = {}) {
	ctx.outputsMask = 1;
	ScheduledAction events[] = {
			{getTimeNowNt() + US2NT(startUs), {scheduledStartInjection, ctx}},
			{getTimeNowNt() + US2NT(endUs), {scheduledEndInjection, ctx}},
	};
	return scheduleFuelCallbacks(events, efi::size(events));
}
} // namespace

TEST(airmassInjectionGate, StartupRequiresPositiveRpmAndCompletePublication) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	selectComposite();
	auto token = gate().beginCalculation(LM_SD_ALPHA_N, 0, engine->getGlobalConfigurationVersion());
	gate().rejectCalculation(Fault::Sensor);
	gate().acceptCalculation();
	gate().completeCalculation(token, true);
	EXPECT_EQ(Status::NotReady, gate().status());
	EXPECT_EQ(Fault::None, gate().fault());
	EXPECT_FALSE(gate().allowInjection());

	token = beginPositiveCalculation();
	gate().acceptCalculation();
	EXPECT_FALSE(gate().allowInjection());
	gate().completeCalculation(token, true);
	EXPECT_TRUE(gate().allowInjection());
	EXPECT_EQ(static_cast<uint8_t>(Status::Ready), engine->outputChannels.blendedStatus);
	EXPECT_TRUE(gate().allowPrime());
}

TEST(airmassInjectionGate, SensorFaultRecoversOnFreshPublicationWithoutStopping) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	selectComposite();
	makeReady();
	gate().rejectCalculation(Fault::Sensor);
	EXPECT_FALSE(getLimpManager()->allowInjection().value);
	EXPECT_EQ(ClearReason::Airmass, getLimpManager()->allowInjection().reason);
	EXPECT_EQ(Status::Faulted, gate().status());
	EXPECT_EQ(Fault::Sensor, gate().fault());
	// The same running RPM recovers; no stop transition or operator action.
	makeReady();
	EXPECT_TRUE(gate().allowInjection());
	EXPECT_EQ(Fault::None, gate().fault());
}

TEST(airmassInjectionGate, DegradedPublicationAdmitsFuelAndNormalPublicationClearsFallback) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	selectComposite();
	auto token = beginPositiveCalculation();
	gate().acceptCalculation(Fault::Sensor);
	gate().completeCalculation(token, true);
	ASSERT_EQ(Status::Degraded, gate().status());
	EXPECT_EQ(Fault::Sensor, gate().fault());
	EXPECT_TRUE(gate().allowInjection());
	EXPECT_TRUE(engine->engineState.veAnalyzeSessionInvalid);
	ASSERT_TRUE(queuePulse(100, 200));
	eth.moveTimeForwardAndInvokeEventsUs(200);
	makeReady();
	EXPECT_EQ(Fault::None, gate().fault());
	EXPECT_TRUE(gate().allowInjection());
}

TEST(airmassInjectionGate, StopAndConfigWriteInvalidateOldPublication) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	selectComposite();
	makeReady();
	auto old = beginPositiveCalculation();
	gate().acceptCalculation();
	gate().onEngineStop();
	gate().completeCalculation(old, true);
	EXPECT_EQ(Status::NotReady, gate().status());
	EXPECT_EQ(Fault::None, gate().fault());
	makeReady();

	old = beginPositiveCalculation();
	gate().acceptCalculation();
	gate().onConfigurationWrite(LM_SD_ALPHA_N, false);
	EXPECT_FALSE(gate().isCalculationCurrent(old));
	gate().completeCalculation(old, true);
	EXPECT_EQ(Status::NotReady, gate().status());
	makeReady();
}

TEST(airmassInjectionGate, StrategyChangedAwayAndBackCannotReuseToken) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	selectComposite();
	makeReady();
	auto old = beginPositiveCalculation();
	gate().acceptCalculation();
	gate().onConfigurationWrite(LM_SPEED_DENSITY, true);
	engineConfiguration->fuelAlgorithm = LM_SPEED_DENSITY;
	gate().onConfigurationWrite(LM_SD_ALPHA_N, true);
	engineConfiguration->fuelAlgorithm = LM_SD_ALPHA_N;
	gate().completeCalculation(old, true);
	EXPECT_EQ(Status::NotReady, gate().status());
	EXPECT_FALSE(gate().allowInjection());
	makeReady();
	EXPECT_TRUE(gate().allowInjection());
}

TEST(airmassInjectionGate, InvalidFinalPublicationRecoversOnlyOnFreshValidFuel) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	selectComposite();
	auto token = beginPositiveCalculation();
	gate().acceptCalculation();
	Sensor::setMockValue(SensorType::Rpm, NAN);
	gate().completeCalculation(token, true);
	EXPECT_EQ(Status::NotReady, gate().status());
	token = beginPositiveCalculation();
	gate().acceptCalculation();
	gate().completeCalculation(token, false);
	EXPECT_EQ(Status::Faulted, gate().status());
	EXPECT_EQ(Fault::Result, gate().fault());
	makeReady();
	EXPECT_TRUE(gate().allowInjection());
}

TEST(airmassInjectionGate, AcceptedLegacyPulseDrainsAcrossStrategyChange) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	ASSERT_TRUE(queuePulse(1000, 3000));
	EXPECT_EQ(2, gate().pendingCallbacks());
	selectComposite();
	EXPECT_FALSE(gate().allowInjection());
	EXPECT_EQ(Status::NotReady, gate().status());
	// New strategy may publish while old callbacks still own their pulses.
	makeReady();
	ASSERT_TRUE(queuePulse(2000, 4000));
	eth.moveTimeForwardAndInvokeEventsUs(2000);
	EXPECT_EQ(2, enginePins.injectors[0].getOverlappingCounter());
	eth.moveTimeForwardAndInvokeEventsUs(1000);
	EXPECT_EQ(1, enginePins.injectors[0].getOverlappingCounter());
	eth.moveTimeForwardAndInvokeEventsUs(1000);
	EXPECT_EQ(0, enginePins.injectors[0].getOverlappingCounter());
	EXPECT_EQ(0, gate().pendingCallbacks());
}

TEST(airmassInjectionGate, OverlappingPrimaryAndStage2CallbacksRemainBalancedAfterFault) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	selectComposite();
	makeReady();
	InjectorContext ctx;
	ctx.outputsMask = 1;
	ctx.stage2Active = true;
	ScheduledAction staged[] = {
			{getTimeNowNt() + US2NT(1000), {scheduledStartInjection, ctx}},
			{getTimeNowNt() + US2NT(2500), {scheduledEndInjectionStage2, ctx}},
			{getTimeNowNt() + US2NT(3000), {scheduledEndInjection, ctx}},
	};
	ASSERT_TRUE(scheduleFuelCallbacks(staged, efi::size(staged)));
	ASSERT_TRUE(queuePulse(2000, 4000));
	gate().rejectCalculation(Fault::Load);
	eth.moveTimeForwardAndInvokeEventsUs(2000);
	EXPECT_EQ(2, enginePins.injectors[0].getOverlappingCounter());
	EXPECT_EQ(1, enginePins.injectorsStage2[0].getOverlappingCounter());
	eth.moveTimeForwardAndInvokeEventsUs(1000);
	EXPECT_EQ(1, enginePins.injectors[0].getOverlappingCounter());
	EXPECT_EQ(0, enginePins.injectorsStage2[0].getOverlappingCounter());
	eth.moveTimeForwardAndInvokeEventsUs(1000);
	EXPECT_EQ(0, enginePins.injectors[0].getOverlappingCounter());
	EXPECT_EQ(0, gate().pendingCallbacks());
}

TEST(airmassInjectionGate, FaultPreventsNewSplitButQueuedSplitDrains) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	selectComposite();
	makeReady();
	InjectorContext ctx;
	ctx.splitDurationUs = 1000;
	ASSERT_TRUE(queuePulse(1000, 2000, ctx));
	gate().rejectCalculation(Fault::Sensor);
	eth.moveTimeForwardAndInvokeEventsUs(2000);
	EXPECT_EQ(0, engine->scheduler.size());
	EXPECT_EQ(0, gate().pendingCallbacks());
	EXPECT_EQ(0, enginePins.injectors[0].getOverlappingCounter());

	makeReady();
	ASSERT_TRUE(queuePulse(1000, 2000, ctx));
	eth.moveTimeForwardAndInvokeEventsUs(2000);
	ASSERT_EQ(2, gate().pendingCallbacks()); // Second half already accepted.
	gate().rejectCalculation(Fault::Sensor);
	eth.moveTimeForwardAndInvokeEventsUs(3000);
	EXPECT_EQ(0, gate().pendingCallbacks());
	EXPECT_EQ(0, enginePins.injectors[0].getOverlappingCounter());
}

TEST(airmassInjectionGate, CompositePrimeRetainsUpstreamStartupBehavior) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	MockInjectorModel2 injector;
	engine->module<InjectorModelPrimary>().set(&injector);
	EXPECT_CALL(injector, getInjectionDuration(testing::_)).WillOnce(Return(20.0f));
	engineConfiguration->primingDelay = 0;
	engine->module<PrimeController>()->onIgnitionStateChanged(true);
	ASSERT_EQ(1, gate().pendingCallbacks());
	selectComposite();
	eth.moveTimeForwardAndInvokeEventsUs(100000);
	EXPECT_TRUE(engine->module<PrimeController>()->isPriming());
	EXPECT_EQ(1, enginePins.injectors[0].getOverlappingCounter());
	eth.moveTimeForwardAndInvokeEventsUs(20000);
	EXPECT_EQ(0, gate().pendingCallbacks());
	EXPECT_EQ(0, enginePins.injectors[0].getOverlappingCounter());
	EXPECT_FALSE(engine->module<PrimeController>()->isPriming());
	EXPECT_EQ(0, engine->scheduler.size());
}

TEST(airmassInjectionGate, ActivePrimeClosesCapturedMaskAfterConfigurationChange) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	MockInjectorModel2 injector;
	engine->module<InjectorModelPrimary>().set(&injector);
	EXPECT_CALL(injector, getInjectionDuration(testing::_)).WillOnce(Return(20.0f));
	engine->engineState.cylinderCount = 4;
	engine->module<PrimeController>()->onPrimeStart();
	eth.executeActions();
	ASSERT_EQ(1, enginePins.injectors[3].getOverlappingCounter());
	engine->engineState.cylinderCount = 2;
	selectComposite();
	eth.moveTimeForwardAndInvokeEventsUs(20000);
	EXPECT_EQ(0, enginePins.injectors[3].getOverlappingCounter());
	EXPECT_EQ(0, gate().pendingCallbacks());
	EXPECT_FALSE(engine->module<PrimeController>()->isPriming());
}

TEST(airmassInjectionGate, InvalidRpmCannotAuthorizeRecovery) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	selectComposite();
	makeReady();
	gate().rejectCalculation(Fault::Sensor);
	for (float rpm : {0.0f, -1.0f, NAN}) {
		Sensor::setMockValue(SensorType::Rpm, rpm);
		auto token = gate().beginCalculation(LM_SD_ALPHA_N, rpm, engine->getGlobalConfigurationVersion());
		gate().acceptCalculation();
		gate().completeCalculation(token, true);
		EXPECT_FALSE(gate().allowInjection());
	}
	makeReady();
	EXPECT_TRUE(gate().allowInjection());
}

TEST(airmassInjectionGate, AccountingPrecedesImmediateCallbacks) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	class ImmediateExecutor : public Scheduler {
	public:
		void schedule(const char*, scheduling_s*, efitick_t, action_s) override {
			ADD_FAILURE() << "fuel must use atomic batch admission";
		}
		void cancel(scheduling_s*) override {}
		bool scheduleBatch(const ScheduledAction* events, size_t count) override {
			EXPECT_EQ(2u, count);
			EXPECT_EQ(2, gate().pendingCallbacks());
			auto open = events[0].action;
			open.execute();
			EXPECT_EQ(1, gate().pendingCallbacks());
			auto close = events[1].action;
			close.execute();
			EXPECT_EQ(0, gate().pendingCallbacks());
			return true;
		}
	} executor;
	engine->scheduler.setMockExecutor(&executor);
	ASSERT_TRUE(queuePulse(0, 1000));
	EXPECT_EQ(0, gate().pendingCallbacks());
	EXPECT_EQ(0, enginePins.injectors[0].getOverlappingCounter());
	engine->scheduler.setMockExecutor(nullptr);
}

TEST(airmassInjectionGate, ExhaustedPoolAdmitsNoPulseAndRollsBackAccounting) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	selectComposite();
	makeReady();
	for (size_t i = 0; i < 64; i++) {
		getScheduler()->schedule("fill", nullptr, getTimeNowNt() + US2NT(10000), {+[](void*) {}, nullptr});
	}
	ASSERT_EQ(64, engine->scheduler.size());
	EXPECT_FALSE(queuePulse(0, 1000));
	EXPECT_EQ(0, gate().pendingCallbacks());
	EXPECT_EQ(0, enginePins.injectors[0].getOverlappingCounter());
	EXPECT_EQ(Status::Faulted, gate().status());
	EXPECT_EQ(Fault::Scheduling, gate().fault());
	eth.moveTimeForwardAndInvokeEventsUs(10000);
	makeReady();
	ASSERT_TRUE(queuePulse(100, 200));
	eth.moveTimeForwardAndInvokeEventsUs(200);
	EXPECT_EQ(0, gate().pendingCallbacks());
}

TEST(airmassInjectionGate, StandaloneModelsRequireValidPublicationAndRecoverWithoutRearm) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	for (auto mode : {LM_SPEED_DENSITY, LM_ALPHA_N, LM_REAL_MAF}) {
		SCOPED_TRACE(static_cast<int>(mode));
		engineConfiguration->fuelAlgorithm = mode;
		EXPECT_FALSE(gate().allowInjection());
		EXPECT_TRUE(gate().allowPrime());
		auto token = beginPositiveCalculation();
		engine->engineState.airmassCalculationValid = true;
		gate().completeCalculation(token, true);
		ASSERT_TRUE(gate().allowInjection());
		ASSERT_EQ(Status::Legacy, gate().status());

		token = beginPositiveCalculation();
		EXPECT_TRUE(gate().allowInjection());
		engine->engineState.airmassCalculationValid = false;
		gate().completeCalculation(token, true);
		EXPECT_FALSE(gate().allowInjection());
		EXPECT_TRUE(gate().allowPrime());
		EXPECT_EQ(Status::Faulted, gate().status());
		EXPECT_EQ(Fault::Result, gate().fault());

		token = beginPositiveCalculation();
		engine->engineState.airmassCalculationValid = true;
		gate().completeCalculation(token, false);
		EXPECT_FALSE(gate().allowInjection());

		token = beginPositiveCalculation();
		engine->engineState.airmassCalculationValid = true;
		gate().completeCalculation(token, true);
		EXPECT_TRUE(gate().allowInjection());
	}
}

TEST(airmassInjectionGate, StandaloneRejectDrainsAcceptedPulseAndBlocksWallFuelAndNewSplit) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->fuelAlgorithm = LM_SPEED_DENSITY;
	auto token = beginPositiveCalculation();
	engine->engineState.airmassCalculationValid = true;
	gate().completeCalculation(token, true);
	InjectorContext ctx;
	ctx.splitDurationUs = 1000;
	ASSERT_TRUE(queuePulse(1000, 2000, ctx));
	gate().rejectCalculation(Fault::Load);
	EXPECT_FALSE(gate().allowInjection());
	EXPECT_FALSE(queuePulse(1000, 3000));
	EXPECT_EQ(Status::Faulted, gate().status());

	// Even stale positive cylinder fuel and a wall-film state cannot get as far
	// as duration calculation once the standalone admission gate has closed.
	engine->cylinders[0].setInjectionMass(1);
	testing::StrictMock<MockInjectorModel2> injector;
	engine->module<InjectorModelPrimary>().set(&injector);
	InjectionEvent event;
	event.injectionStartAngle = 90;
	event.onTriggerTooth({getTimeNowNt(), 0, 180, 0, 180});
	EXPECT_EQ(2, gate().pendingCallbacks());
	eth.moveTimeForwardAndInvokeEventsUs(1000);
	EXPECT_EQ(1, enginePins.injectors[0].getOverlappingCounter());
	eth.moveTimeForwardAndInvokeEventsUs(1000);
	EXPECT_EQ(0, enginePins.injectors[0].getOverlappingCounter());
	EXPECT_EQ(0, gate().pendingCallbacks());
	EXPECT_EQ(0, engine->scheduler.size());
}

TEST(airmassInjectionGate, StandaloneStopAndTuneWriteRejectStaleCompletion) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->fuelAlgorithm = LM_ALPHA_N;
	auto old = beginPositiveCalculation();
	gate().onConfigurationWrite(LM_ALPHA_N, false);
	engine->engineState.airmassCalculationValid = true;
	gate().completeCalculation(old, true);
	EXPECT_FALSE(gate().allowInjection());
	auto current = beginPositiveCalculation();
	engine->engineState.airmassCalculationValid = true;
	gate().completeCalculation(current, true);
	EXPECT_TRUE(gate().allowInjection());
	gate().completeCalculation(old, false);
	EXPECT_TRUE(gate().allowInjection());

	gate().onEngineStop();
	EXPECT_FALSE(gate().allowInjection());
	EXPECT_TRUE(gate().allowPrime());
	gate().completeCalculation(current, true);
	EXPECT_FALSE(gate().allowInjection());
}

TEST(airmassInjectionGate, StandaloneKeepsCompletedFuelAvailableDuringRecalculation) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	for (auto mode : {LM_SPEED_DENSITY, LM_ALPHA_N, LM_REAL_MAF}) {
		SCOPED_TRACE(static_cast<int>(mode));
		engineConfiguration->fuelAlgorithm = mode;
		auto token = beginPositiveCalculation();
		engine->engineState.airmassCalculationValid = true;
		gate().completeCalculation(token, true);
		ASSERT_TRUE(gate().allowInjection());

		// Model evaluation clears its working validity flag before computing a
		// replacement. Trigger interrupts must still schedule the completed fuel
		// publication, even if they repeatedly coincide with this calculation.
		for (int cycle = 0; cycle < 3; cycle++) {
			token = beginPositiveCalculation();
			engine->engineState.airmassCalculationValid = false;
			ASSERT_TRUE(queuePulse(100, 200));
			eth.moveTimeForwardAndInvokeEventsUs(100);
			EXPECT_EQ(1, enginePins.injectors[0].getOverlappingCounter());
			engine->engineState.airmassCalculationValid = true;
			gate().completeCalculation(token, true);
			eth.moveTimeForwardAndInvokeEventsUs(100);
			EXPECT_EQ(0, enginePins.injectors[0].getOverlappingCounter());
			EXPECT_EQ(0, gate().pendingCallbacks());
		}

		// An actual input failure closes admission before completion. Accepted
		// closing edges still drain, and a stale completion cannot reopen it.
		token = beginPositiveCalculation();
		ASSERT_TRUE(queuePulse(100, 200));
		eth.moveTimeForwardAndInvokeEventsUs(100);
		gate().rejectCalculation(Fault::Sensor);
		EXPECT_FALSE(queuePulse(10, 20));
		engine->engineState.airmassCalculationValid = true;
		gate().completeCalculation(token, true);
		EXPECT_FALSE(gate().allowInjection());
		eth.moveTimeForwardAndInvokeEventsUs(100);
		EXPECT_EQ(0, enginePins.injectors[0].getOverlappingCounter());
		EXPECT_EQ(0, gate().pendingCallbacks());
	}
}

TEST(airmassInjectionGate, StandaloneRecalculationCannotRetainFuelAcrossInvalidRpmOrVersion) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->fuelAlgorithm = LM_SPEED_DENSITY;
	for (float invalidRpm : {0.0f, -1.0f, NAN}) {
		auto token = beginPositiveCalculation();
		engine->engineState.airmassCalculationValid = true;
		gate().completeCalculation(token, true);
		ASSERT_TRUE(gate().allowInjection());
		gate().beginCalculation(LM_SPEED_DENSITY, invalidRpm, engine->getGlobalConfigurationVersion());
		EXPECT_FALSE(gate().allowInjection());
	}

	auto token = beginPositiveCalculation();
	engine->engineState.airmassCalculationValid = true;
	gate().completeCalculation(token, true);
	ASSERT_TRUE(gate().allowInjection());
	gate().beginCalculation(LM_SPEED_DENSITY, 1000, engine->getGlobalConfigurationVersion() + 1);
	EXPECT_FALSE(gate().allowInjection());
	// Returning to the old version does not restore the old admission state.
	beginPositiveCalculation();
	EXPECT_FALSE(gate().allowInjection());
}

TEST(airmassInjectionGate, CompositeStaleCompletionCannotCloseNewerReadyPublication) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	selectComposite();
	auto old = beginPositiveCalculation();
	gate().acceptCalculation();
	makeReady();
	gate().completeCalculation(old, false);
	EXPECT_TRUE(gate().allowInjection());
	EXPECT_EQ(Status::Ready, gate().status());
}

TEST(airmassInjectionGate, CompositeVersionChangeRequiresFreshPublication) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	selectComposite();
	makeReady();
	gate().beginCalculation(LM_SD_ALPHA_N, 1000, engine->getGlobalConfigurationVersion() + 1);
	EXPECT_FALSE(gate().allowInjection());
	EXPECT_EQ(Status::NotReady, gate().status());
	beginPositiveCalculation();
	EXPECT_FALSE(gate().allowInjection());
	makeReady();
	EXPECT_TRUE(gate().allowInjection());
}

TEST(airmassInjectionGate, ExecutorValidatesEachAdmittedBatchOnceAndRollsBackInvalidWork) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	selectComposite();
	makeReady();
	const auto validations = scheduleBatchValidationCount;
	ASSERT_TRUE(queuePulse(100, 200));
	EXPECT_EQ(scheduleBatchValidationCount, validations + 1);
	ASSERT_EQ(gate().pendingCallbacks(), 2);
	const auto now = getTimeNowNt();
	ScheduledAction valid[] = {{now + US2NT(100), {+[](void*) {}, nullptr}}};
	ScheduledAction missingAction[] = {{now, {}}};
	ScheduledAction unsorted[] = {{now + 2, valid[0].action}, {now + 1, valid[0].action}};
	ScheduledAction tooFar[] = {{now + US2NT(MaximumScheduleDelayUs), valid[0].action}};
	ScheduledAction overflow[] = {{efitick_t{INT64_MAX}, valid[0].action}};

	const auto reject = [&](const ScheduledAction* events, size_t count) {
		makeReady();
		const auto before = scheduleBatchValidationCount;
		EXPECT_FALSE(scheduleFuelCallbacks(events, count));
		EXPECT_EQ(scheduleBatchValidationCount, before + 1);
		EXPECT_EQ(gate().pendingCallbacks(), 2);
		EXPECT_EQ(engine->scheduler.size(), 2);
		EXPECT_EQ(gate().fault(), Fault::Scheduling);
		EXPECT_FALSE(gate().allowInjection());
		EXPECT_EQ(engine->outputChannels.blendedStatus, static_cast<uint8_t>(Status::Faulted));
		EXPECT_EQ(engine->outputChannels.blendedFault, static_cast<uint8_t>(Fault::Scheduling));
	};
	reject(nullptr, 1);
	reject(valid, 0);
	reject(valid, MaxScheduleBatchSize + 1);
	reject(missingAction, efi::size(missingAction));
	reject(unsorted, efi::size(unsorted));
	reject(tooFar, efi::size(tooFar));
	reject(overflow, efi::size(overflow));

	// The previously accepted open/close drain even after every new batch faults.
	eth.moveTimeForwardAndInvokeEventsUs(200);
	EXPECT_EQ(gate().pendingCallbacks(), 0);
	EXPECT_EQ(enginePins.injectors[0].getOverlappingCounter(), 0);
	makeReady();
	ASSERT_TRUE(queuePulse(100, 200));
	eth.moveTimeForwardAndInvokeEventsUs(200);
	EXPECT_EQ(gate().pendingCallbacks(), 0);
}

TEST(airmassInjectionGate, CallbackCounterOverflowRejectsBeforeInsertionWithoutLosingPendingWork) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	selectComposite();
	makeReady();
	{
		chibios_rt::CriticalSectionLocker csl;
		ASSERT_TRUE(gate().locked(csl).callbacksAccepted(UINT16_MAX - 1));
	}
	EXPECT_FALSE(queuePulse(100, 200));
	EXPECT_EQ(gate().pendingCallbacks(), UINT16_MAX - 1);
	EXPECT_EQ(engine->scheduler.size(), 0);
	EXPECT_EQ(gate().fault(), Fault::Scheduling);
	{
		chibios_rt::CriticalSectionLocker csl;
		gate().locked(csl).callbacksRejected(UINT16_MAX - 1);
	}
}
