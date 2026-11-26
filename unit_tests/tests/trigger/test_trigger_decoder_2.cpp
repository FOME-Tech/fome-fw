#include "pch.h"

using ::testing::_;
using ::testing::NiceMock;
using ::testing::StrictMock;

class MockTriggerConfiguration : public TriggerConfiguration {
public:
	MockTriggerConfiguration(trigger_config_s type)
		: TriggerConfiguration("Mock")
		, m_type(type) {}

protected:
	bool isVerboseTriggerSynchDetails() const override {
		return false;
	}

	trigger_config_s getType() const override {
		return m_type;
	}

private:
	const trigger_config_s m_type;
};

struct MockTriggerDecoder : public TriggerDecoderBase {
	MockTriggerDecoder()
		: TriggerDecoderBase("mock") {}

	MOCK_METHOD(void, onTriggerError, (), (override));
	MOCK_METHOD(void, onNotEnoughTeeth, (int actual, int expected), (override));
	MOCK_METHOD(void, onTooManyTeeth, (int actual, int expected), (override));
};

static void makeTriggerShape(TriggerWaveform& shape, operation_mode_e mode, const TriggerConfiguration& cfg) {
	shape.initializeTriggerWaveform(mode, cfg);
}

#define doTooth(dut, shape, cfg, t) dut.decodeTriggerEvent("", shape, nullptr, cfg, TriggerEvent::PrimaryRising, t)

TEST(TriggerDecoder, FindsFirstSyncPoint) {
	MockTriggerConfiguration cfg({trigger_type_e::TT_TOOTHED_WHEEL, 4, 1});
	cfg.update();

	PrimaryTriggerWaveform shape;
	makeTriggerShape(shape, FOUR_STROKE_CAM_SENSOR, cfg);

	efitick_t t = 0;

	// Strict so it complains about unexpected calls to onTriggerError()
	StrictMock<MockTriggerDecoder> dut;

	// Fire a few boring evenly spaced teeth
	t += MS2NT(1);
	doTooth(dut, shape, cfg, t);
	EXPECT_FALSE(dut.getShaftSynchronized());
	EXPECT_EQ(0, dut.currentCycle.current_index);

	t += MS2NT(1);
	doTooth(dut, shape, cfg, t);
	EXPECT_FALSE(dut.getShaftSynchronized());
	EXPECT_EQ(0, dut.currentCycle.current_index);

	t += MS2NT(1);
	doTooth(dut, shape, cfg, t);
	EXPECT_FALSE(dut.getShaftSynchronized());
	EXPECT_EQ(0, dut.currentCycle.current_index);

	// Missing tooth, 2x normal length!
	t += MS2NT(2);
	doTooth(dut, shape, cfg, t);
	EXPECT_TRUE(dut.getShaftSynchronized());
	EXPECT_EQ(0, dut.currentCycle.current_index);

	// Normal tooth after the missing tooth, should be index 2 now (one rise + one fall)
	t += MS2NT(1);
	doTooth(dut, shape, cfg, t);
	EXPECT_TRUE(dut.getShaftSynchronized());
	EXPECT_EQ(2, dut.currentCycle.current_index);

	EXPECT_FALSE(dut.someSortOfTriggerError());
}

TEST(TriggerDecoder, FindsSyncPointMultipleRevolutions) {
	MockTriggerConfiguration cfg({trigger_type_e::TT_TOOTHED_WHEEL, 4, 1});
	cfg.update();

	PrimaryTriggerWaveform shape;
	makeTriggerShape(shape, FOUR_STROKE_CAM_SENSOR, cfg);

	efitick_t t = 0;

	// Strict so it complains about unexpected calls to onTriggerError()
	StrictMock<MockTriggerDecoder> dut;

	// normal tooth
	t += MS2NT(1);
	doTooth(dut, shape, cfg, t);

	// normal tooth
	t += MS2NT(1);
	doTooth(dut, shape, cfg, t);

	// Missing tooth, 2x normal length!
	t += MS2NT(2);
	doTooth(dut, shape, cfg, t);
	EXPECT_TRUE(dut.getShaftSynchronized());
	EXPECT_EQ(0, dut.currentCycle.current_index);
	EXPECT_EQ(0, dut.getCrankSynchronizationCounter());

	// Do 100 turns and make sure we stay synchronized
	for (int i = 0; i < 100; i++) {
		// normal tooth
		t += MS2NT(1);
		doTooth(dut, shape, cfg, t);
		EXPECT_EQ(2, dut.currentCycle.current_index);

		// normal tooth
		t += MS2NT(1);
		doTooth(dut, shape, cfg, t);
		EXPECT_EQ(4, dut.currentCycle.current_index);

		// Missing tooth, 2x normal length!
		t += MS2NT(2);
		doTooth(dut, shape, cfg, t);
		EXPECT_TRUE(dut.getShaftSynchronized());
		EXPECT_EQ(0, dut.currentCycle.current_index);
		EXPECT_FALSE(dut.someSortOfTriggerError());

		// We do one revolution per loop iteration
		EXPECT_EQ(i + 1, dut.getCrankSynchronizationCounter());
	}
}

TEST(TriggerDecoder, TooManyTeeth_CausesError) {
	MockTriggerConfiguration cfg({trigger_type_e::TT_TOOTHED_WHEEL, 4, 1});
	cfg.update();

	PrimaryTriggerWaveform shape;
	makeTriggerShape(shape, FOUR_STROKE_CAM_SENSOR, cfg);

	efitick_t t = 0;

	StrictMock<MockTriggerDecoder> dut;
	// We expect one call to onTriggerError().
	EXPECT_CALL(dut, onTriggerError());
	EXPECT_CALL(dut, onTooManyTeeth(_, _));

	// Fire a few boring evenly spaced teeth
	t += MS2NT(1);
	doTooth(dut, shape, cfg, t);
	EXPECT_FALSE(dut.getShaftSynchronized());

	t += MS2NT(1);
	doTooth(dut, shape, cfg, t);
	EXPECT_FALSE(dut.getShaftSynchronized());

	t += MS2NT(1);
	doTooth(dut, shape, cfg, t);
	EXPECT_FALSE(dut.getShaftSynchronized());

	// Missing tooth, 2x normal length!
	t += MS2NT(2);
	doTooth(dut, shape, cfg, t);
	EXPECT_TRUE(dut.getShaftSynchronized());
	EXPECT_EQ(0, dut.currentCycle.current_index);

	// One good revolution, so that the sync point is confirmed and errors are reported
	t += MS2NT(1);
	doTooth(dut, shape, cfg, t);
	t += MS2NT(1);
	doTooth(dut, shape, cfg, t);
	t += MS2NT(2);
	doTooth(dut, shape, cfg, t);
	EXPECT_TRUE(dut.isSyncConfirmed());
	EXPECT_EQ(0, dut.currentCycle.current_index);

	t += MS2NT(1);
	doTooth(dut, shape, cfg, t);
	EXPECT_TRUE(dut.getShaftSynchronized());
	EXPECT_EQ(2, dut.currentCycle.current_index);

	t += MS2NT(1);
	doTooth(dut, shape, cfg, t);
	EXPECT_TRUE(dut.getShaftSynchronized());
	EXPECT_EQ(4, dut.currentCycle.current_index);
	EXPECT_EQ(0, dut.triggerErrorCounter);

	// This tooth is extra - expect a call to onTriggerError() and loss of sync!
	t += MS2NT(1);
	doTooth(dut, shape, cfg, t);
	EXPECT_FALSE(dut.getShaftSynchronized());
	EXPECT_EQ(6, dut.currentCycle.current_index);
	EXPECT_EQ(1, dut.triggerErrorCounter);

	// Fire some normal revolutions to ensure we recover without additional error types.
	for (size_t i = 0; i < 10; i++) {
		// Missing tooth, 2x normal length!
		t += MS2NT(2);
		doTooth(dut, shape, cfg, t);
		EXPECT_TRUE(dut.getShaftSynchronized());
		EXPECT_EQ(0, dut.currentCycle.current_index);

		// normal tooth
		t += MS2NT(1);
		doTooth(dut, shape, cfg, t);
		EXPECT_EQ(2, dut.currentCycle.current_index);

		// normal tooth
		t += MS2NT(1);
		doTooth(dut, shape, cfg, t);
		EXPECT_EQ(4, dut.currentCycle.current_index);
	}

	// Should now have sync again
	EXPECT_TRUE(dut.getShaftSynchronized());
}

TEST(TriggerDecoder, NotEnoughTeeth_CausesError) {
	MockTriggerConfiguration cfg({trigger_type_e::TT_TOOTHED_WHEEL, 4, 1});
	cfg.update();

	PrimaryTriggerWaveform shape;
	makeTriggerShape(shape, FOUR_STROKE_CAM_SENSOR, cfg);

	efitick_t t = 0;

	StrictMock<MockTriggerDecoder> dut;
	// We expect one call to onTriggerError().
	EXPECT_CALL(dut, onTriggerError());
	EXPECT_CALL(dut, onNotEnoughTeeth(_, _));

	// Fire a few boring evenly spaced teeth
	t += MS2NT(1);
	doTooth(dut, shape, cfg, t);
	EXPECT_FALSE(dut.getShaftSynchronized());

	t += MS2NT(1);
	doTooth(dut, shape, cfg, t);
	EXPECT_FALSE(dut.getShaftSynchronized());

	t += MS2NT(1);
	doTooth(dut, shape, cfg, t);
	EXPECT_FALSE(dut.getShaftSynchronized());

	// Missing tooth, 2x normal length!
	t += MS2NT(2);
	doTooth(dut, shape, cfg, t);
	EXPECT_TRUE(dut.getShaftSynchronized());
	EXPECT_EQ(0, dut.currentCycle.current_index);

	// One good revolution, so that the sync point is confirmed and errors are reported
	t += MS2NT(1);
	doTooth(dut, shape, cfg, t);
	t += MS2NT(1);
	doTooth(dut, shape, cfg, t);
	t += MS2NT(2);
	doTooth(dut, shape, cfg, t);
	EXPECT_TRUE(dut.isSyncConfirmed());
	EXPECT_EQ(0, dut.currentCycle.current_index);

	t += MS2NT(1);
	doTooth(dut, shape, cfg, t);
	EXPECT_TRUE(dut.getShaftSynchronized());
	EXPECT_EQ(2, dut.currentCycle.current_index);
	EXPECT_FALSE(dut.someSortOfTriggerError());
	EXPECT_EQ(0, dut.triggerErrorCounter);

	// Missing tooth, but it comes early - not enough teeth have happened yet!
	t += MS2NT(2);
	doTooth(dut, shape, cfg, t);

	// Sync is lost until we get to another sync point
	EXPECT_FALSE(dut.getShaftSynchronized());
	EXPECT_EQ(0, dut.currentCycle.current_index);
	EXPECT_EQ(1, dut.triggerErrorCounter);
	EXPECT_TRUE(dut.someSortOfTriggerError());

	// Fire some normal revolutions to ensure we recover without additional error types.
	for (size_t i = 0; i < 10; i++) {
		// normal tooth
		t += MS2NT(1);
		doTooth(dut, shape, cfg, t);
		EXPECT_EQ(i == 0 ? 0 : 2, dut.currentCycle.current_index);

		// normal tooth
		t += MS2NT(1);
		doTooth(dut, shape, cfg, t);
		EXPECT_EQ(i == 0 ? 0 : 4, dut.currentCycle.current_index);

		// Missing tooth, 2x normal length!
		t += MS2NT(2);
		doTooth(dut, shape, cfg, t);
		EXPECT_TRUE(dut.getShaftSynchronized());
		EXPECT_EQ(0, dut.currentCycle.current_index);
	}

	// Should now have sync again
	EXPECT_TRUE(dut.getShaftSynchronized());
}

// The first sync point after starting to spin isn't checked against anything: if the next one comes too
// early or too late, it's most likely that the first one was wrong, as happens during cranking, rather than
// that something went wrong since. Sync is dropped as always, but it's not reported as a trigger error.
TEST(TriggerDecoder, TooManyTeethBeforeSyncConfirmed_NotReported) {
	MockTriggerConfiguration cfg({trigger_type_e::TT_TOOTHED_WHEEL, 4, 1});
	cfg.update();

	PrimaryTriggerWaveform shape;
	makeTriggerShape(shape, FOUR_STROKE_CAM_SENSOR, cfg);

	efitick_t t = 0;

	StrictMock<MockTriggerDecoder> dut;
	// Sync is lost, but no onTooManyTeeth()
	EXPECT_CALL(dut, onTriggerError());

	for (int i = 0; i < 3; i++) {
		t += MS2NT(1);
		doTooth(dut, shape, cfg, t);
	}

	// Missing tooth: the first sync point
	t += MS2NT(2);
	doTooth(dut, shape, cfg, t);
	EXPECT_TRUE(dut.getShaftSynchronized());
	EXPECT_FALSE(dut.isSyncConfirmed());

	// Three teeth: one too many
	for (int i = 0; i < 3; i++) {
		t += MS2NT(1);
		doTooth(dut, shape, cfg, t);
	}

	EXPECT_FALSE(dut.getShaftSynchronized());
	EXPECT_EQ(0, dut.triggerErrorCounter);
	EXPECT_FALSE(dut.someSortOfTriggerError());

	// Normal from here on: sync comes back and is confirmed
	for (int i = 0; i < 2; i++) {
		t += MS2NT(2);
		doTooth(dut, shape, cfg, t);
		t += MS2NT(1);
		doTooth(dut, shape, cfg, t);
		t += MS2NT(1);
		doTooth(dut, shape, cfg, t);
	}
	t += MS2NT(2);
	doTooth(dut, shape, cfg, t);
	EXPECT_TRUE(dut.getShaftSynchronized());
	EXPECT_TRUE(dut.isSyncConfirmed());
	EXPECT_EQ(0, dut.triggerErrorCounter);
}

TEST(TriggerDecoder, NotEnoughTeethBeforeSyncConfirmed_NotReported) {
	MockTriggerConfiguration cfg({trigger_type_e::TT_TOOTHED_WHEEL, 4, 1});
	cfg.update();

	PrimaryTriggerWaveform shape;
	makeTriggerShape(shape, FOUR_STROKE_CAM_SENSOR, cfg);

	efitick_t t = 0;

	StrictMock<MockTriggerDecoder> dut;
	// Sync is lost, but no onNotEnoughTeeth()
	EXPECT_CALL(dut, onTriggerError());

	for (int i = 0; i < 3; i++) {
		t += MS2NT(1);
		doTooth(dut, shape, cfg, t);
	}

	// Missing tooth: the first sync point
	t += MS2NT(2);
	doTooth(dut, shape, cfg, t);
	EXPECT_TRUE(dut.getShaftSynchronized());
	EXPECT_FALSE(dut.isSyncConfirmed());

	// One tooth, then a gap that looks like the sync point, but too early
	t += MS2NT(1);
	doTooth(dut, shape, cfg, t);
	t += MS2NT(2);
	doTooth(dut, shape, cfg, t);

	EXPECT_FALSE(dut.getShaftSynchronized());
	EXPECT_EQ(0, dut.triggerErrorCounter);
	EXPECT_FALSE(dut.someSortOfTriggerError());
}

// Once confirmed, losing sync and getting it back starts over: the first sync point after an error isn't
// confirmed either, so errors are reported again only after a good revolution
TEST(TriggerDecoder, SyncConfirmationStartsOverAfterError) {
	MockTriggerConfiguration cfg({trigger_type_e::TT_TOOTHED_WHEEL, 4, 1});
	cfg.update();

	PrimaryTriggerWaveform shape;
	makeTriggerShape(shape, FOUR_STROKE_CAM_SENSOR, cfg);

	efitick_t t = 0;

	NiceMock<MockTriggerDecoder> dut;

	auto goodRevolution = [&]() {
		t += MS2NT(1);
		doTooth(dut, shape, cfg, t);
		t += MS2NT(1);
		doTooth(dut, shape, cfg, t);
		t += MS2NT(2);
		doTooth(dut, shape, cfg, t);
	};

	goodRevolution();
	EXPECT_FALSE(dut.isSyncConfirmed());
	goodRevolution();
	EXPECT_TRUE(dut.isSyncConfirmed());

	// An extra tooth
	t += MS2NT(1);
	doTooth(dut, shape, cfg, t);
	goodRevolution();
	EXPECT_EQ(1, dut.triggerErrorCounter);
	EXPECT_FALSE(dut.isSyncConfirmed());

	goodRevolution();
	EXPECT_TRUE(dut.getShaftSynchronized());
	EXPECT_TRUE(dut.isSyncConfirmed());
	EXPECT_EQ(1, dut.triggerErrorCounter);
}

TEST(TriggerDecoder, PrimaryDecoderNoDisambiguation) {
	MockTriggerConfiguration cfg({trigger_type_e::TT_TOOTHED_WHEEL, 4, 1});
	cfg.update();

	PrimaryTriggerWaveform shape;
	makeTriggerShape(shape, FOUR_STROKE_CAM_SENSOR, cfg);

	efitick_t t = 0;

	PrimaryTriggerDecoder dut("test");

	// This is not the right place for this, but further refactoring has to happen before it can get moved.
	dut.setNeedsDisambiguation(shape.needsDisambiguation(), true);

	// Fire a few boring evenly spaced teeth
	t += MS2NT(1);
	doTooth(dut, shape, cfg, t);
	t += MS2NT(1);
	doTooth(dut, shape, cfg, t);
	t += MS2NT(1);
	doTooth(dut, shape, cfg, t);

	// Missing tooth, 2x normal length!
	t += MS2NT(2);
	doTooth(dut, shape, cfg, t);

	// Is synchronized
	EXPECT_TRUE(dut.getShaftSynchronized());
	// Has "full" sync, doesn't need cam information to sync
	EXPECT_TRUE(dut.hasSynchronizedPhase());
}

TEST(TriggerDecoder, PrimaryDecoderNeedsDisambiguation) {
	MockTriggerConfiguration cfg({trigger_type_e::TT_TOOTHED_WHEEL, 4, 1});
	cfg.update();

	PrimaryTriggerWaveform shape;
	makeTriggerShape(shape, FOUR_STROKE_CRANK_SENSOR, cfg);

	efitick_t t = 0;

	PrimaryTriggerDecoder dut("test");

	// This is not the right place for this, but further refactoring has to happen before it can get moved.
	dut.setNeedsDisambiguation(shape.needsDisambiguation(), true);

	// Fire a few boring evenly spaced teeth
	t += MS2NT(1);
	doTooth(dut, shape, cfg, t);
	t += MS2NT(1);
	doTooth(dut, shape, cfg, t);
	t += MS2NT(1);
	doTooth(dut, shape, cfg, t);

	// Missing tooth, 2x normal length!
	t += MS2NT(2);
	doTooth(dut, shape, cfg, t);

	// Is synchronized
	EXPECT_TRUE(dut.getShaftSynchronized());
	// Does not have full sync, this trigger is ambiguous
	EXPECT_FALSE(dut.hasSynchronizedPhase());

	// Provide cam assist information to the primary trigger
	dut.syncEnginePhase(2, 0, 720);

	// We now have full sync!
	EXPECT_TRUE(dut.hasSynchronizedPhase());

	// If there's a trigger error, we lose full sync
	// Tests above ensure onTriggerError() is called properly
	dut.onTriggerError();
	EXPECT_FALSE(dut.hasSynchronizedPhase());
}
