/**
 * @file	test_trigger_decoder.cpp
 *
 * Tests of the trigger decoder and of trigger shape definitions by themselves.
 *
 * Tests of a whole running engine (RPM, spark and injection outputs) live elsewhere and use the
 * simulated engine in harness/engine_sim.h: see test_engine_invariants.cpp,
 * ignition_injection/test_spark_outputs.cpp and ignition_injection/test_injection_outputs.cpp.
 */

#include "pch.h"

#include "trigger_decoder.h"
#include "trigger_chrysler.h"
#include "trigger_universal.h"

// Feed a decoder directly: a wheel that needs no sync point is synchronized by its first tooth,
// then just counts teeth around the revolution
TEST(trigger, decoderCountsTeethOfWheelWithoutSyncPoint) {
	EngineTestHelper eth(engine_type_e::FORD_INLINE_6_1995);

	TriggerDecoderBase state_("test");
	TriggerDecoderBase* sta = &state_;

	const auto& triggerConfiguration = engine->triggerCentral.primaryTriggerConfiguration;

	ASSERT_FALSE(sta->shaft_is_synchronized) << "shaft_is_synchronized";
	int r = 10;
	sta->decodeTriggerEvent(
			"t",
			engine->triggerCentral.triggerShape,
			/* override */ nullptr,
			triggerConfiguration,
			TriggerEvent::PrimaryRising,
			++r);
	ASSERT_TRUE(sta->shaft_is_synchronized); // first signal rise synchronize
	ASSERT_EQ(0, sta->getCurrentIndex());

	for (int i = 2; i < 10; i += 2) {
		sta->decodeTriggerEvent(
				"t",
				engine->triggerCentral.triggerShape,
				/* override */ nullptr,
				triggerConfiguration,
				TriggerEvent::PrimaryRising,
				r++);
		EXPECT_EQ(i, sta->getCurrentIndex());
	}

	sta->decodeTriggerEvent(
			"test",
			engine->triggerCentral.triggerShape,
			/* override */ nullptr,
			triggerConfiguration,
			TriggerEvent::PrimaryRising,
			r++);
	ASSERT_EQ(10, sta->getCurrentIndex());

	sta->decodeTriggerEvent(
			"test",
			engine->triggerCentral.triggerShape,
			/* override */ nullptr,
			triggerConfiguration,
			TriggerEvent::PrimaryRising,
			r++);
	ASSERT_EQ(0, sta->getCurrentIndex()); // new revolution
}

extern TriggerDecoderBase initState;

// The sync point of an engine type's trigger is where it's expected to be, and (optionally) the gap
// ratio the decoder measures there is what the trigger definition intends
static void expectSyncPoint(const char* msg, engine_type_e type, int synchPointIndex, float expectedGapRatio = NAN) {
	// Some configs use aux valves, which requires this sensor
	std::unordered_map<SensorType, float> sensorVals = {{SensorType::DriverThrottleIntent, 0}};
	EngineTestHelper eth(type, sensorVals);

	TriggerWaveform* t = &engine->triggerCentral.triggerShape;

	ASSERT_FALSE(t->shapeDefinitionError) << msg;

	ASSERT_EQ(synchPointIndex, t->getTriggerWaveformSynchPointIndex()) << msg;
	if (!std::isnan(expectedGapRatio)) {
		EXPECT_NEAR(expectedGapRatio, initState.triggerSyncGapRatio, 0.001) << msg;
	}
}

TEST(trigger, skippedToothWheelShape) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	TriggerWaveform* s = &engine->triggerCentral.triggerShape;

	// Two evenly spaced teeth: four edges, a quarter of the cycle apart
	initializeSkippedToothTrigger(s, 2, 0, FOUR_STROKE_CAM_SENSOR, SyncEdge::Rise);
	EXPECT_EQ(s->getSize(), 4);
	ASSERT_EQ(s->wave.getSwitchTime(0), 0.25);
	ASSERT_EQ(s->wave.getSwitchTime(1), 0.5);
	ASSERT_EQ(s->wave.getSwitchTime(2), 0.75);
	ASSERT_EQ(s->wave.getSwitchTime(3), 1);
}

TEST(trigger, syncPointsOfEngineTypes) {
	expectSyncPoint("Miata 2003", engine_type_e::FRANKENSO_MAZDA_MIATA_2003, 3);

	expectSyncPoint("Ford inline 6", engine_type_e::FORD_INLINE_6_1995, 0);

	expectSyncPoint("Dodge Neon 1995", engine_type_e::DODGE_NEON_1995, 0);

	expectSyncPoint("ford aspire", engine_type_e::FORD_ASPIRE_1996, 4);

	expectSyncPoint("Miata NB2", engine_type_e::HELLEN_NB2, 3);

	expectSyncPoint("test 2/1 both", engine_type_e::TEST_ISSUE_366_BOTH, 0);
	expectSyncPoint("test 2/1 rise", engine_type_e::TEST_ISSUE_366_RISE, 0);

	expectSyncPoint("test engine", engine_type_e::TEST_ENGINE, 0);

	expectSyncPoint("testFordEscortGt", engine_type_e::FORD_ESCORT_GT, 0);

	expectSyncPoint("test1+1", engine_type_e::DEFAULT_FRANKENSO, 0);

	expectSyncPoint("testCitroen", engine_type_e::CITROEN_TU3JP, 0);
	expectSyncPoint("citroen", engine_type_e::CITROEN_TU3JP, 0, 2.9994);

	expectSyncPoint("CAMARO_4", engine_type_e::CAMARO_4, 40);

	expectSyncPoint("neon NGC4", engine_type_e::DODGE_NEON_2003_CRANK, 6, CHRYSLER_NGC4_GAP);

	{
		EngineTestHelper eth(engine_type_e::DODGE_NEON_2003_CRANK);

		applyNonPersistentConfiguration();
		prepareOutputSignals();
	}

	expectSyncPoint("sachs", engine_type_e::SACHS, 0);

	expectSyncPoint("vw ABA", engine_type_e::VW_ABA, 0);
}
