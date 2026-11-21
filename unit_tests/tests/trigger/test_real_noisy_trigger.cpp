#include "pch.h"
#include "logicdata_csv_reader.h"
#include "spark_logic.h"

#include "harness/engine_invariants.h"

static void testNoOverdwell(const char* file) {
	CsvReader reader(1, /* vvtCount */ 0);

	reader.open(file);
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->isFasterEngineSpinUpEnabled = true;

	setCylinderCount(6);
	engineConfiguration->firingOrder = FO_1_5_3_6_2_4;
	engineConfiguration->globalTriggerAngleOffset = 155;
	engineConfiguration->ignitionMode = IM_INDIVIDUAL_COILS;

	// setCylinderCount(1);
	// engineConfiguration->firingOrder = FO_1;
	// engineConfiguration->globalTriggerAngleOffset = 35;

	engineConfiguration->timingMode = TM_FIXED;
	engineConfiguration->fixedTiming = 14.69;

	engineConfiguration->trigger.customTotalToothCount = 36;
	engineConfiguration->trigger.customSkippedToothCount = 2;
	eth.setTriggerType(trigger_type_e::TT_TOOTHED_WHEEL);

	engineConfiguration->isIgnitionEnabled = true;
	engineConfiguration->isInjectionEnabled = false;

	OutputRecorder recorder;

	while (reader.haveMore()) {
		reader.processLine(&eth);
	}

	// The engine did fire (these logs are short, a couple of dozen sparks)
	EXPECT_GT(recorder.pulses(OutputKind::Coil).size(), 10u);

	// However bad the trigger signal got, no coil was left charging: neither between two
	// sparks, nor still on at the end of the log
	EXPECT_TRUE(noOverdwell(recorder, 50));
}

// This 36-2 wheel has enough teeth that instant RPM is used automatically, there's no
// longer a non-instant case to compare against
TEST(RealNoisyTrigger, AvoidOverdwell1) {
	testNoOverdwell("tests/trigger/resources/noisy-trigger-1.csv");
}

TEST(RealNoisyTrigger, AvoidOverdwell2) {
	testNoOverdwell("tests/trigger/resources/noisy-trigger-2.csv");
}
