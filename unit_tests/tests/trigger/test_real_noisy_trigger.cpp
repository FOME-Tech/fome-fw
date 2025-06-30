#include "pch.h"
#include "logicdata_csv_reader.h"
#include "spark_logic.h"

static void testNoOverdwell(const char* file, bool instantRpm) {
	CsvReader reader(1, /* vvtCount */ 0);

	reader.open(file);
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->isFasterEngineSpinUpEnabled = true;
	engineConfiguration->alwaysInstantRpm = instantRpm;

	engineConfiguration->cylindersCount = 6;
	engineConfiguration->firingOrder = FO_1_5_3_6_2_4;
	engineConfiguration->globalTriggerAngleOffset = 155;
	engineConfiguration->ignitionMode = IM_INDIVIDUAL_COILS;

	// engineConfiguration->cylindersCount = 1;
	// engineConfiguration->firingOrder = FO_1;
	// engineConfiguration->globalTriggerAngleOffset = 35;

	engineConfiguration->timingMode = TM_FIXED;
	engineConfiguration->fixedTiming = 14.69;

	engineConfiguration->trigger.customTotalToothCount = 36;
	engineConfiguration->trigger.customSkippedToothCount = 2;
	eth.setTriggerType(trigger_type_e::TT_TOOTHED_WHEEL);

	engineConfiguration->isIgnitionEnabled = true;
	engineConfiguration->isInjectionEnabled = false;

	std::vector<efitick_t> coilStartTimes(12);

	engine->onIgnitionEvent = [&](const IgnitionContext& ctx, bool state) {
		auto nowNt = getTimeNowNt();

		int cyl = 0;
		uint16_t mask = ctx.outputsMask;
		while (mask) {
			if (mask & 0x1) {
				if (state) {
					coilStartTimes[cyl] = nowNt;
				} else {
					auto actualDwell = 1e-3 * NT2USF(nowNt - coilStartTimes[cyl]);

					EXPECT_LT(actualDwell, 50) << "Overdwell on cylinder " << (int)cyl << " of " << actualDwell << " ms";
				}
			}

			mask = mask >> 1;
			cyl++;
		}
	};

	while (reader.haveMore()) {
		reader.processLine(&eth);
	}

	// nothing to check here, just that no coils got stuck on
}

TEST(RealNoisyTrigger, AvoidOverdwell1NoInstant) {
	testNoOverdwell("tests/trigger/resources/noisy-trigger-1.csv", false);
}

TEST(RealNoisyTrigger, AvoidOverdwell1WithInstant) {
	testNoOverdwell("tests/trigger/resources/noisy-trigger-1.csv", true);
}

TEST(RealNoisyTrigger, AvoidOverdwell2NoInstant) {
	testNoOverdwell("tests/trigger/resources/noisy-trigger-2.csv", false);
}

TEST(RealNoisyTrigger, AvoidOverdwell2WithInstant) {
	testNoOverdwell("tests/trigger/resources/noisy-trigger-2.csv", true);
}
