/*
 * @file test_multispark.cpp
 *
 * @date Mar 15, 2020
 * @author Matthew Kennedy, (c) 2020
 */

#include "pch.h"
#include "advance_map.h"

TEST(Multispark, DefaultConfiguration) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	EXPECT_EQ(0, getMultiSparkCount(0    ));
	EXPECT_EQ(0, getMultiSparkCount(100  ));
	EXPECT_EQ(0, getMultiSparkCount(200  ));
	EXPECT_EQ(0, getMultiSparkCount(500  ));
	EXPECT_EQ(0, getMultiSparkCount(1000  ));
	EXPECT_EQ(0, getMultiSparkCount(2000  ));
	EXPECT_EQ(0, getMultiSparkCount(5000  ));
	EXPECT_EQ(0, getMultiSparkCount(50000 ));
}

static void multisparkCfg() {
	// Turn it on!
	engineConfiguration->multisparkEnable = true;

	// Fire up to 45 degrees worth of sparks...
	engineConfiguration->multisparkMaxSparkingAngle = 45;

	// ...but limit to 10 additional sparks
	engineConfiguration->multisparkMaxExtraSparkCount = 10;

	// 3ms period (spark + dwell)
	engineConfiguration->multisparkDwell = 2;
	engineConfiguration->multisparkSparkDuration = 1;
}

TEST(Multispark, EnabledNoMaxRpm) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	multisparkCfg();

	// Practically no RPM limit
	engineConfiguration->multisparkMaxRpm = 12500;

	EXPECT_EQ(0,  getMultiSparkCount(0    ));
	EXPECT_EQ(10, getMultiSparkCount(150  ));
	EXPECT_EQ(10, getMultiSparkCount(250  ));
	EXPECT_EQ(4,  getMultiSparkCount(550  ));
	EXPECT_EQ(3,  getMultiSparkCount(800  ));
	EXPECT_EQ(2,  getMultiSparkCount(900  ));
	EXPECT_EQ(1,  getMultiSparkCount(1500  ));

	// 2500 is the threshold where we should get zero
	EXPECT_EQ(1,  getMultiSparkCount(2499  ));
	EXPECT_EQ(0,  getMultiSparkCount(2501  ));

	EXPECT_EQ(0,  getMultiSparkCount(5000  ));

	EXPECT_EQ(0,  getMultiSparkCount(50000 ));
}

TEST(Multispark, RpmLimit) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	multisparkCfg();

	// Disable at 800 rpm
	engineConfiguration->multisparkMaxRpm = 800;

	EXPECT_EQ(3, getMultiSparkCount(795));
	EXPECT_EQ(0, getMultiSparkCount(805));
}
