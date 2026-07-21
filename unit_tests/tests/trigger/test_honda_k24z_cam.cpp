#include "pch.h"

// gap that follows each tooth of the K24Z exhaust cam, in cam degrees, tooth 0 is the sync tooth
static const float k24zGaps[] = {102.1f, 167.8f, 90.1f};

TEST(HondaK24Z, exhaustCamSync) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	eth.setTriggerType(trigger_type_e::TT_HONDA_K24Z_CAM_3);

	const auto& triggerConfiguration = engine->triggerCentral.primaryTriggerConfiguration;
	const TriggerWaveform& shape = engine->triggerCentral.triggerShape;

	ASSERT_TRUE(shape.isSynchronizationNeeded);
	ASSERT_EQ(6, shape.getSize());

	TriggerDecoderBase state("k24z");

	efitick_t nowNt = 1000;
	// one cam degree worth of time, at some arbitrary speed
	constexpr efitick_t ntPerDegree = 1000;

	// Start feeding teeth partway in to the pattern so that sync has to actually find the
	// right tooth rather than getting it for free on the first edge.
	int toothIndex = 1;

	bool everSynchronized = false;

	for (int i = 0; i < 30; i++) {
		state.decodeTriggerEvent(
				"k24z",
				shape,
				/* override */ nullptr,
				triggerConfiguration,
				TriggerEvent::PrimaryRising,
				nowNt);

		if (state.getShaftSynchronized()) {
			if (!everSynchronized) {
				// we must have synced on the tooth that follows the biggest gap
				EXPECT_EQ(0, toothIndex) << "sync tooth, iteration " << i;
				everSynchronized = true;
			}

			// index advances by two per tooth since we only handle rising edges
			EXPECT_EQ(2 * toothIndex, state.getCurrentIndex()) << "index, iteration " << i;
		}

		nowNt += ntPerDegree * k24zGaps[toothIndex];
		toothIndex = (toothIndex + 1) % 3;
	}

	EXPECT_TRUE(everSynchronized);
	EXPECT_EQ(0, eth.recentWarnings()->getCount());
}
