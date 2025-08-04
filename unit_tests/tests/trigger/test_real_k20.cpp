#include "pch.h"

#include "logicdata_csv_reader.h"

static int getExhaustIndex() {
	return getTriggerCentral()->vvtState[/*bankIndex*/0][/*camIndex*/1].currentCycle.current_index;
}

TEST(realk20, cranking) {
	CsvReader reader(/* triggerCount */ 1, /* vvtCount */ 2);

	reader.open("tests/trigger/resources/civic-K20-cranking.csv", NORMAL_ORDER, REVERSE_ORDER);
	reader.twoBanksSingleCamMode = false;

	EngineTestHelper eth(engine_type_e::PROTEUS_HONDA_K);

	while (reader.haveMore()) {
		reader.processLine(&eth);

		auto vvtI = engine->triggerCentral.getVVTPosition(/*bankIndex*/0, /*camIndex*/0);
		if (vvtI) {
			EXPECT_TRUE(vvtI.Value > -20 && vvtI.Value < 10) << "VVT angle: " << vvtI.Value;
		}

		auto vvtE = engine->triggerCentral.getVVTPosition(/*bankIndex*/0, /*camIndex*/1);
		if (vvtE) {
			EXPECT_TRUE(vvtE.Value > -10 && vvtE.Value < 10);
		}

	}

	EXPECT_EQ(1192, round(Sensor::getOrZero(SensorType::Rpm)));
	EXPECT_TRUE(getTriggerCentral()->triggerState.hasSynchronizedPhase());
}
