/*
 * @file test_instant_rpm_auto.cpp
 *
 * Instant RPM is used automatically for triggers with 24 or more teeth per engine cycle,
 * see shouldUseInstantRpm() in rpm_calculator.cpp
 */

#include "pch.h"

static size_t getPrimaryTeethPerCycle(trigger_type_e type, bool skippedWheelOnCam = false) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	engineConfiguration->skippedWheelOnCam = skippedWheelOnCam;
	eth.setTriggerType(type);

	return engine->triggerCentral.triggerShape.primaryTeethPerCycle;
}

TEST(instantRpm, teethPerEngineCycle) {
	// One tooth on the crank means two teeth per engine cycle
	EXPECT_EQ(2u, getPrimaryTeethPerCycle(trigger_type_e::TT_ONE));

	// A 12 tooth crank wheel is exactly the cutoff for automatic instant RPM
	EXPECT_EQ(24u, getPrimaryTeethPerCycle(trigger_type_e::TT_12_TOOTH_CRANK));

	// Honda K has 12 evenly spaced teeth plus one extra
	EXPECT_EQ(26u, getPrimaryTeethPerCycle(trigger_type_e::TT_HONDA_K_CRANK_12_1));

	// 58 teeth on the crank, twice per engine cycle
	EXPECT_EQ(116u, getPrimaryTeethPerCycle(trigger_type_e::TT_TOOTHED_WHEEL_60_2));

	// The same wheel on the cam only turns once per engine cycle
	EXPECT_EQ(58u, getPrimaryTeethPerCycle(trigger_type_e::TT_TOOTHED_WHEEL_60_2, /*skippedWheelOnCam*/ true));

	// Only the primary wheel counts, the 4 teeth of this trigger's cam wheel don't help
	EXPECT_EQ(2u, getPrimaryTeethPerCycle(trigger_type_e::TT_MAZDA_MIATA_NA));
}
