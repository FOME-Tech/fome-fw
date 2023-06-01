/*
 * test_dwell_corner_case_issue_796.cpp
 *
 *  Created on: Jul 1, 2019
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"

TEST(scheduler, dwellIssue796) {

	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setupSimpleTestEngineWithMafAndTT_ONE_trigger(&eth, IM_SEQUENTIAL);

	eth.fireTriggerEvents2(4 /* count */ , 600 /* ms */);

	// check if the mode is changed
	ASSERT_EQ(CRANKING, engine->rpmCalculator.getState());
	// due to isFasterEngineSpinUp=true, we should have already detected RPM!
	ASSERT_EQ( 100,  round(Sensor::getOrZero(SensorType::Rpm))) << "spinning-RPM#1";
	ASSERT_NEAR(300000.0f, engine->rpmCalculator.oneDegreeUs * 180, 1);

	// with just a bit much time between events integer RPM goes down one full percent
	eth.smartFireRise(601);
	eth.smartFireFall(600);
	ASSERT_NEAR( 100,  round(Sensor::getOrZero(SensorType::Rpm)), EPS3D) << "spinning-RPM#2";
	// while integer RPM value is 1% away from rpm=100, below oneDegreeUs is much closer to RPM=100 value
	ASSERT_NEAR(300250, (int)(engine->rpmCalculator.oneDegreeUs * 180), 1);
}
