#include "pch.h"
#include "main_trigger_callback.h"
#include "injector_model.h"

using ::testing::_;
using ::testing::InSequence;
using ::testing::StrictMock;

using ::testing::Eq;
using ::testing::Not;
using ::testing::Property;
using ::testing::Truly;

namespace {
struct ExpectedScheduledAction {
	efitick_t time;
	void (*callback)(void*);
	void* argument;
};

void expectBatch(MockExecutor& executor, std::initializer_list<ExpectedScheduledAction> expected) {
	std::vector<ExpectedScheduledAction> copied(expected);
	EXPECT_CALL(executor, scheduleBatch(_, copied.size()))
			.WillOnce([copied](const ScheduledAction* events, size_t count) {
				EXPECT_EQ(count, copied.size());
				for (size_t i = 0; i < count; i++) {
					EXPECT_EQ(events[i].time, copied[i].time);
					EXPECT_EQ(events[i].action.getCallback(), copied[i].callback);
					EXPECT_EQ(events[i].action.getArgument(), copied[i].argument);
				}
				return true;
			});
}
} // namespace

TEST(injectionScheduling, InjectionIsScheduled) {
	StrictMock<MockExecutor> mockExec;

	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engine->scheduler.setMockExecutor(&mockExec);

	efitick_t nowNt = 1000000;

	InjectionEvent event;

	// Injection duration of 20ms
	MockInjectorModel2 im;
	EXPECT_CALL(im, getInjectionDuration(_)).WillOnce(Return(20.0f));
	engine->module<InjectorModelPrimary>().set(&im);

	engine->rpmCalculator.oneDegreeUs = 100;

	InjectorContext ctx;
	ctx.outputsMask = (1 << 0);
	ctx.eventIndex = 0;
	ctx.stage2Active = false;

	void* ctxAsPtr = bit_cast<void*>(ctx);

	// Should reserve one normal injection as an atomic batch.
	float nt5deg = USF2NT(engine->rpmCalculator.oneDegreeUs * 5);
	efitick_t startTime = nowNt + nt5deg;
	efitick_t endTime = startTime + MS2NT(20);
	expectBatch(
			mockExec,
			{{startTime, (void (*)(void*))scheduledStartInjection, ctxAsPtr},
			 {endTime, (void (*)(void*))scheduledEndInjection, ctxAsPtr}});

	// Event scheduled at 125 degrees
	event.injectionStartAngle = 125;

	// We are at 120 degrees now, next tooth 130
	event.onTriggerTooth({nowNt, 0, 0, 120, 130});
}

TEST(injectionScheduling, InjectionIsScheduledDualStage) {
	StrictMock<MockExecutor> mockExec;
	StrictMock<MockInjectorModel2> im;

	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engine->scheduler.setMockExecutor(&mockExec);
	engine->module<InjectorModelPrimary>().set(&im);
	engine->module<InjectorModelSecondary>().set(&im);

	efitick_t nowNt = 1000000;

	InjectionEvent event;

	InjectorContext ctx;
	ctx.outputsMask = (1 << 0);
	ctx.eventIndex = 0;
	ctx.stage2Active = true;

	void* ctxAsPtr = bit_cast<void*>(ctx);

	engine->rpmCalculator.oneDegreeUs = 100;

	// Some nonzero fuel quantity on both stages
	engine->cylinders[0].setInjectionMass(50);
	engine->engineState.injectionStage2Fraction = 0.2;

	{
		InSequence is;

		// Primary injection duration of 20ms, secondary 10ms
		EXPECT_CALL(im, getInjectionDuration(40)).WillOnce(Return(20.0f));
		EXPECT_CALL(im, getInjectionDuration(10)).WillOnce(Return(10.0f));
	}

	// The two closing edges are sorted chronologically inside the atomic batch.
	float nt5deg = USF2NT(engine->rpmCalculator.oneDegreeUs * 5);
	efitick_t startTime = nowNt + nt5deg;
	efitick_t stage2EndTime = startTime + MS2NT(10);
	efitick_t primaryEndTime = startTime + MS2NT(20);
	expectBatch(
			mockExec,
			{{startTime, (void (*)(void*))scheduledStartInjection, ctxAsPtr},
			 {stage2EndTime, (void (*)(void*))scheduledEndInjectionStage2, ctxAsPtr},
			 {primaryEndTime, (void (*)(void*))scheduledEndInjection, ctxAsPtr}});

	// Event scheduled at 125 degrees
	event.injectionStartAngle = 125;

	// We are at 120 degrees now, next tooth 130
	event.onTriggerTooth({nowNt, 0, 0, 120, 130});
}

TEST(injectionScheduling, InjectionIsScheduledBeforeWraparound) {
	StrictMock<MockExecutor> mockExec;

	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engine->scheduler.setMockExecutor(&mockExec);

	efitick_t nowNt = 1000000;

	InjectionEvent event;

	InjectorContext ctx;
	ctx.outputsMask = (1 << 0);
	ctx.eventIndex = 0;
	ctx.stage2Active = false;

	void* ctxAsPtr = bit_cast<void*>(ctx);

	// Injection duration of 20ms
	MockInjectorModel2 im;
	EXPECT_CALL(im, getInjectionDuration(_)).WillOnce(Return(20.0f));
	engine->module<InjectorModelPrimary>().set(&im);

	engine->rpmCalculator.oneDegreeUs = 100;

	float nt5deg = USF2NT(engine->rpmCalculator.oneDegreeUs * 5);
	efitick_t startTime = nowNt + nt5deg;
	efitick_t endTime = startTime + MS2NT(20);
	expectBatch(
			mockExec,
			{{startTime, (void (*)(void*))scheduledStartInjection, ctxAsPtr},
			 {endTime, (void (*)(void*))scheduledEndInjection, ctxAsPtr}});

	// Event scheduled at 715 degrees
	event.injectionStartAngle = 715;

	// We are at 710 degrees now, next tooth 010
	event.onTriggerTooth({nowNt, 0, 0, 710, 010});
}

TEST(injectionScheduling, InjectionIsScheduledAfterWraparound) {
	StrictMock<MockExecutor> mockExec;

	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engine->scheduler.setMockExecutor(&mockExec);

	efitick_t nowNt = 1000000;

	InjectionEvent event;

	InjectorContext ctx;
	ctx.outputsMask = (1 << 0);
	ctx.eventIndex = 0;
	ctx.stage2Active = false;

	void* ctxAsPtr = bit_cast<void*>(ctx);

	// Injection duration of 20ms
	MockInjectorModel2 im;
	EXPECT_CALL(im, getInjectionDuration(_)).WillOnce(Return(20.0f));
	engine->module<InjectorModelPrimary>().set(&im);

	engine->rpmCalculator.oneDegreeUs = 100;

	float nt5deg = USF2NT(engine->rpmCalculator.oneDegreeUs * 15);
	efitick_t startTime = nowNt + nt5deg;
	efitick_t endTime = startTime + MS2NT(20);
	expectBatch(
			mockExec,
			{{startTime, (void (*)(void*))scheduledStartInjection, ctxAsPtr},
			 {endTime, (void (*)(void*))scheduledEndInjection, ctxAsPtr}});

	// Event scheduled at 5 degrees
	event.injectionStartAngle = 5;

	// We are at 710 degrees now, next tooth 010
	event.onTriggerTooth({nowNt, 0, 0, 710, 010});
}

TEST(injectionScheduling, InjectionNotScheduled) {
	// StrictMock since we expect no scheduler calls!
	StrictMock<MockExecutor> mockExec;

	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engine->scheduler.setMockExecutor(&mockExec);

	efitick_t nowNt = 1000000;

	InjectionEvent event;

	// Expect no calls to injector model
	StrictMock<MockInjectorModel2> im;
	engine->module<InjectorModelPrimary>().set(&im);

	engine->rpmCalculator.oneDegreeUs = 100;

	{
		InSequence is;

		// Expect no scheduler calls!
	}

	// Event scheduled at 125 degrees
	event.injectionStartAngle = 125;

	// We are at 130 degrees now, next tooth 140
	event.onTriggerTooth({nowNt, 0, 0, 130, 140});
}

TEST(injectionScheduling, SplitInjectionScheduled) {
	StrictMock<MockExecutor> mockExec;

	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engine->scheduler.setMockExecutor(&mockExec);

	InjectionEvent event;

	InjectorContext ctx;
	ctx.outputsMask = 0;
	ctx.eventIndex = 0;
	ctx.stage2Active = false;

	void* ctxAsPtr = bit_cast<void*>(ctx);

	// Split injection events should be called with no remaining split duration
	ctx.splitDurationUs = 0;

	// Should reserve the second half of split injection as a batch.
	efitick_t nowNt = getTimeNowNt();
	efitick_t startTime = nowNt + MS2NT(2);
	efitick_t endTime = startTime + MS2NT(10);
	expectBatch(
			mockExec,
			{{startTime, (void (*)(void*))scheduledStartInjection, ctxAsPtr},
			 {endTime, (void (*)(void*))scheduledEndInjection, ctxAsPtr}});

	// Split injection duration of 10ms
	ctx.splitDurationUs = 10000;

	// Close injector, should cause second half of split injection to be scheduled!
	endInjection(ctx);
}
