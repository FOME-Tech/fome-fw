#include "pch.h"

// gap that follows each tooth of the Honda J intake cam, in degrees, tooth 0 is the sync tooth.
// Six evenly spaced slots with two missing, so three normal gaps and one three times as wide.
static const float hondaJGaps[] = {120, 120, 120, 360};

TEST(HondaJ, intakeCamSync) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	eth.setTriggerType(trigger_type_e::TT_HONDA_J_CAM_6_2);

	const auto& triggerConfiguration = engine->triggerCentral.primaryTriggerConfiguration;
	const TriggerWaveform& shape = engine->triggerCentral.triggerShape;

	ASSERT_TRUE(shape.isSynchronizationNeeded);
	// four remaining teeth, rise and fall for each
	ASSERT_EQ(8, shape.getSize());

	TriggerDecoderBase state("hondaJ");

	efitick_t nowNt = 1000;
	// one degree worth of time, at some arbitrary speed
	constexpr efitick_t ntPerDegree = 1000;

	// Start feeding teeth partway in to the pattern so that sync has to actually find the
	// right tooth rather than getting it for free on the first edge.
	int toothIndex = 1;

	bool everSynchronized = false;

	for (int i = 0; i < 30; i++) {
		state.decodeTriggerEvent(
				"hondaJ",
				shape,
				/* override */ nullptr,
				triggerConfiguration,
				TriggerEvent::PrimaryRising,
				nowNt);

		if (state.getShaftSynchronized()) {
			if (!everSynchronized) {
				// we must have synced on the tooth that follows the wide gap
				EXPECT_EQ(0, toothIndex) << "sync tooth, iteration " << i;
				everSynchronized = true;
			}

			// index advances by two per tooth since we only handle rising edges
			EXPECT_EQ(2 * toothIndex, state.getCurrentIndex()) << "index, iteration " << i;
		}

		nowNt += ntPerDegree * hondaJGaps[toothIndex];
		toothIndex = (toothIndex + 1) % 4;
	}

	EXPECT_TRUE(everSynchronized);
	EXPECT_EQ(0, eth.recentWarnings()->getCount());
}
