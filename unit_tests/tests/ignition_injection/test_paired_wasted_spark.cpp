#include "pch.h"
#include "spark_logic.h"

using ::testing::_;

// Verify that on a "normal" wasted-spark engine the output mask sets bits for both companion
// cylinders' coil slots — this is the existing behavior we must preserve.
TEST(pairedOddFireWastedSpark, defaultMaskSetsBothCompanionBits) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	EXPECT_CALL(*eth.mockAirmass, getAirmass(_, _)).WillRepeatedly(Return(AirmassResult{0.1f, 50.0f}));
	setupSimpleTestEngineWithMafAndTT_ONE_trigger(&eth);

	engineConfiguration->cylindersCount = 4;
	engineConfiguration->firingOrder = FO_1_3_4_2;
	engineConfiguration->ignitionMode = IM_WASTED_SPARK;
	engineConfiguration->pairedOddFireWastedSpark = false;
	engineConfiguration->isIgnitionEnabled = true;

	initializeIgnitionActions();

	// Firing order 1-3-4-2 → cyl numbers (0-indexed) 0,2,3,1.
	// Pairs: index 0 (cyl 1, num 0) ↔ index 2 (cyl 4, num 3); index 1 (cyl 3, num 2) ↔ index 3 (cyl 2, num 1).
	EXPECT_EQ(engine->ignitionEvents.elements[0].calculateIgnitionOutputMask(), (uint16_t)((1 << 0) | (1 << 3)));
	EXPECT_EQ(engine->ignitionEvents.elements[1].calculateIgnitionOutputMask(), (uint16_t)((1 << 1) | (1 << 2)));
	EXPECT_EQ(engine->ignitionEvents.elements[2].calculateIgnitionOutputMask(), (uint16_t)((1 << 0) | (1 << 3)));
	EXPECT_EQ(engine->ignitionEvents.elements[3].calculateIgnitionOutputMask(), (uint16_t)((1 << 1) | (1 << 2)));
}

// With pairedOddFireWastedSpark, only the first-half companion's bit is set: a single physical coil is
// driven from both cylinder events, so we use exactly one OutputPin per pair.
TEST(pairedOddFireWastedSpark, maskUsesSingleBitForPair) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	EXPECT_CALL(*eth.mockAirmass, getAirmass(_, _)).WillRepeatedly(Return(AirmassResult{0.1f, 50.0f}));
	setupSimpleTestEngineWithMafAndTT_ONE_trigger(&eth);

	engineConfiguration->cylindersCount = 4;
	engineConfiguration->firingOrder = FO_1_3_4_2;
	engineConfiguration->ignitionMode = IM_WASTED_SPARK;
	engineConfiguration->pairedOddFireWastedSpark = true;
	engineConfiguration->isIgnitionEnabled = true;

	initializeIgnitionActions();

	// Only the lower firing-order companion's bit is set — for both events of the pair.
	// Pair {index 0 = cyl 1 (num 0), index 2 = cyl 4 (num 3)} both map to coil slot 0.
	EXPECT_EQ(engine->ignitionEvents.elements[0].calculateIgnitionOutputMask(), (uint16_t)(1 << 0));
	EXPECT_EQ(engine->ignitionEvents.elements[2].calculateIgnitionOutputMask(), (uint16_t)(1 << 0));
	// Pair {index 1 = cyl 3 (num 2), index 3 = cyl 2 (num 1)} both map to coil slot 2.
	EXPECT_EQ(engine->ignitionEvents.elements[1].calculateIgnitionOutputMask(), (uint16_t)(1 << 2));
	EXPECT_EQ(engine->ignitionEvents.elements[3].calculateIgnitionOutputMask(), (uint16_t)(1 << 2));
}

// On an odd-fire engine, pairedOddFireWastedSpark must suppress the +360 re-fire path (useOddFireWastedSpark)
// since the second fire of each coil happens via the paired cylinder's own event at its natural angle.
TEST(pairedOddFireWastedSpark, suppressesUseOddFireWastedSpark) {
	EngineTestHelper eth(engine_type_e::MINIMAL_PINS);
	engineConfiguration->cylindersCount = 2;
	engineConfiguration->firingOrder = FO_1_2;
	engineConfiguration->ignitionMode = IM_WASTED_SPARK;

	// Make it odd fire — would normally trigger useOddFireWastedSpark.
	engineConfiguration->timing_offset_cylinder[0] = 19;
	engineConfiguration->timing_offset_cylinder[1] = -13;

	engineConfiguration->pairedOddFireWastedSpark = true;

	prepareOutputSignals();
	EXPECT_FALSE(engine->engineState.useOddFireWastedSpark);

	// Sanity: clearing the flag re-enables the legacy path.
	engineConfiguration->pairedOddFireWastedSpark = false;
	prepareOutputSignals();
	EXPECT_TRUE(engine->engineState.useOddFireWastedSpark);
}
