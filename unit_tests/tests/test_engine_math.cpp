/*
 * @file	test_engine_math.c
 *
 * @date Nov 14, 2013
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"

#include "speed_density.h"
#include "maf.h"

TEST(misc, testIgnitionMapGenerator) {
	float rpmBin[16];
	setRpmBin(rpmBin, 16, 800, 7000);
	ASSERT_EQ(650, rpmBin[0]);
	ASSERT_EQ(800, rpmBin[1]) << "@1";
	ASSERT_EQ(1100, rpmBin[2]) << "@2";
	ASSERT_EQ(1400, rpmBin[3]) << "rpm@3";
	ASSERT_EQ(4700, rpmBin[14]) << "rpm@14";
	ASSERT_EQ(7000, rpmBin[15]);

	EXPECT_NEAR(36.0, getInitialAdvance(6000, 100, 36), 0.1);
	EXPECT_NEAR(9.9, getInitialAdvance(600, 100, 36), 0.2);

	EXPECT_NEAR(34.2, getInitialAdvance(2400, 40, 36), 0.1);
	EXPECT_NEAR(41.9, getInitialAdvance(4400, 40, 36), 0.2);
	EXPECT_NEAR(14.2, getInitialAdvance(800, 20, 36), 0.2);
}
