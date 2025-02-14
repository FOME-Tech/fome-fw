/*
 * @file test_real_cranking_nissan_vq40.cpp
 *
 * @date Jul 21, 2021
 * @author Andrey Belomutskiy, (c) 2012-2021
 */

#include "pch.h"
#include "logicdata_csv_reader.h"

static void test(int engineSyncCam, float camOffsetAdd) {
	CsvReader reader(1, /* vvtCount */ 2);

	reader.open("tests/trigger/resources/nissan_vq40_cranking-1.csv");
	EngineTestHelper eth(engine_type_e::HELLEN_121_NISSAN_6_CYL);
	engineConfiguration->isFasterEngineSpinUpEnabled = false;
	engineConfiguration->alwaysInstantRpm = true;

	// Different sync cam may result in different TDC point, so we might need different cam offsets.
	engineConfiguration->vvtOffsets[0] = engineConfiguration->vvtOffsets[0] + camOffsetAdd;
	engineConfiguration->vvtOffsets[2] = engineConfiguration->vvtOffsets[2] + camOffsetAdd;
	engineConfiguration->engineSyncCam = engineSyncCam;

	bool hasSeenFirstVvt = false;

	while (reader.haveMore()) {
		reader.processLine(&eth);
		auto vvt1 = engine->triggerCentral.getVVTPosition(/*bankIndex*/0, /*camIndex*/0);
		auto vvt2 = engine->triggerCentral.getVVTPosition(/*bankIndex*/1, /*camIndex*/0);

		if (vvt1 && vvt1.Value != 0) {
			if (!hasSeenFirstVvt) {
				EXPECT_NEAR(vvt1.Value, 1.4, /*precision*/1);
				hasSeenFirstVvt = true;
			}

			// cam position should never be reported outside of correct range
			EXPECT_TRUE(vvt1.Value > -3 && vvt1.Value < 3);
		}

		if (vvt2) {
			// cam position should never be reported outside of correct range
			EXPECT_TRUE(vvt2.Value > -3 && vvt2.Value < 3);
		}
	}

	EXPECT_NEAR(engine->triggerCentral.getVVTPosition(/*bankIndex*/0, /*camIndex*/0).value_or(0), 1.352, 1e-2);
	EXPECT_NEAR(engine->triggerCentral.getVVTPosition(/*bankIndex*/1, /*camIndex*/0).value_or(0), 1.657, 1e-2);
	ASSERT_EQ(243, round(Sensor::getOrZero(SensorType::Rpm)))<< reader.lineIndex();

	ASSERT_EQ(0, eth.recentWarnings()->getCount());
}

// On Nissan VQ, all cams have the same pattern, so all should be equally good for engine sync. Check them all!

TEST(realCrankingVQ40, normalCrankingSyncCam1) {
	test(0, 0);
}

TEST(realCrankingVQ40, normalCrankingSyncCam2) {
	test(2, -360);
}
