/*
 * @file test_real_nb2_cranking.cpp
 *
 * @date July 13, 2019
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"
#include "logicdata_csv_reader.h"

TEST(realCrankingNB2, normalCranking) {
	CsvReader reader(1, /* vvtCount */ 1);

	reader.open("tests/trigger/resources/nb2-cranking-good.csv");
	EngineTestHelper eth(engine_type_e::HELLEN_NB2);
	engineConfiguration->alwaysInstantRpm = true;

	while (reader.haveMore()) {
		reader.processLine(&eth);
	}

	// VVT position nearly zero!
	EXPECT_NEAR(engine->triggerCentral.getVVTPosition(0, 0).value_or(0), 11.2627f, 1e-4);

	// Check the number of times VVT information was used to adjust crank phase
	// This should happen exactly once: once we sync, we shouldn't lose it.
	EXPECT_EQ(engine->triggerCentral.triggerState.m_camResyncCounter, 1);

	ASSERT_EQ(876, round(Sensor::getOrZero(SensorType::Rpm)));

	EXPECT_EQ(0, eth.recentWarnings()->getCount());
}

TEST(realCrankingNB2, crankingMissingInjector) {
	CsvReader reader(1, /* vvtCount */ 1);

	reader.open("tests/trigger/resources/nb2-cranking-good-missing-injector-1.csv");
	EngineTestHelper eth(engine_type_e::HELLEN_NB2);
	engineConfiguration->alwaysInstantRpm = true;

	while (reader.haveMore()) {
		reader.processLine(&eth);

		if (auto vvt = engine->triggerCentral.getVVTPosition(0, 0)) {
			// VVT position nearly zero!
			EXPECT_NEAR(vvt.Value, 0, 20);
		}
	}

	ASSERT_EQ(316, round(Sensor::getOrZero(SensorType::Rpm)));

	EXPECT_EQ(1, eth.recentWarnings()->getCount());
	EXPECT_EQ(ObdCode::CUSTOM_CAM_TOO_MANY_TEETH, eth.recentWarnings()->get(0).Code);
}
