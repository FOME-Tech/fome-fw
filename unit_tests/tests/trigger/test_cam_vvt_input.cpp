/*
 * test_cam_vvt_input.cpp
 *
 *  Created on: Jan 13, 2019
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"

extern WarningCodeState unitTestWarningCodeState;

#include "engine_sniffer.h"
extern WaveChart waveChart;

TEST(trigger, testNoStartUpWarningsNoSyncronizationTrigger) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	// one tooth does not need synchronization it just counts tooth
	eth.setTriggerType(trigger_type_e::TT_ONE);
	ASSERT_EQ(0, round(Sensor::getOrZero(SensorType::Rpm))) << "testNoStartUpWarnings RPM";

	eth.fireTriggerEvents2(/*count*/ 10, /*duration*/ 50);
	ASSERT_EQ(1200, round(Sensor::getOrZero(SensorType::Rpm))) << "testNoStartUpWarnings RPM";
	ASSERT_EQ(0, unitTestWarningCodeState.recentWarnings.getCount())
			<< "warningCounter#testNoStartUpWarningsNoSyncronizationTrigger";
}

TEST(trigger, testNoStartUpWarnings) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	// for this test we need a trigger with isSynchronizationNeeded=true
	engineConfiguration->trigger.customTotalToothCount = 3;
	engineConfiguration->trigger.customSkippedToothCount = 1;
	eth.setTriggerType(trigger_type_e::TT_TOOTHED_WHEEL);
	// Run the scheduled injections as time passes: left queued, they would use up the scheduler's pool
	ASSERT_EQ(0, round(Sensor::getOrZero(SensorType::Rpm))) << "testNoStartUpWarnings RPM";

	for (int i = 0; i < 10; i++) {
		eth.smartFireRise(50);
		eth.smartFireFall(50);
		eth.smartFireRise(50);
		eth.smartFireFall(150);
	}

	ASSERT_EQ(400, round(Sensor::getOrZero(SensorType::Rpm))) << "testNoStartUpWarnings RPM";
	ASSERT_EQ(0, unitTestWarningCodeState.recentWarnings.getCount()) << "warningCounter#testNoStartUpWarnings";
	// now let's post something unneeded
	eth.smartFireRise(50);
	eth.smartFireFall(50);
	eth.smartFireRise(50); // this is noise
	eth.smartFireFall(50); // this is noise
	eth.smartFireRise(50);
	eth.smartFireFall(150);
	for (int i = 0; i < 1; i++) {
		eth.smartFireRise(50);
		eth.smartFireFall(50);
		eth.smartFireRise(50);
		eth.smartFireFall(150);
	}
	EXPECT_EQ(1, unitTestWarningCodeState.recentWarnings.getCount())
			<< "warningCounter#testNoStartUpWarnings CUSTOM_SYNC_COUNT_MISMATCH expected";
	EXPECT_EQ(ObdCode::CUSTOM_PRIMARY_TOO_MANY_TEETH, unitTestWarningCodeState.recentWarnings.get(0).Code);
}

TEST(trigger, testNoisyInput) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	ASSERT_EQ(0, Sensor::getOrZero(SensorType::Rpm));

	eth.firePrimaryTriggerRise();
	eth.firePrimaryTriggerFall();
	eth.firePrimaryTriggerRise();
	eth.firePrimaryTriggerFall();
	eth.firePrimaryTriggerRise();
	eth.firePrimaryTriggerFall();
	eth.firePrimaryTriggerRise();
	eth.firePrimaryTriggerFall();
	ASSERT_EQ(0, Sensor::getOrZero(SensorType::Rpm));

	// The decoder never had a confirmed sync (two sync points the right number of teeth apart), so this
	// isn't reported as a trigger error: before that, a "wrong" sync point is as likely to be the first
	// one found while starting to spin as anything else
	EXPECT_FALSE(engine->triggerCentral.triggerState.isSyncConfirmed());
	EXPECT_EQ(0, unitTestWarningCodeState.recentWarnings.getCount());
	EXPECT_EQ(0u, engine->triggerCentral.triggerState.triggerErrorCounter);
}

TEST(trigger, testCamInput) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	// changing to 'ONE TOOTH' trigger on CRANK with CAM/VVT
	setCrankOperationMode();
	engineConfiguration->vvtMode[0] = VVT_SINGLE_TOOTH;
	engineConfiguration->vvtOffsets[0] = 360;
	eth.setTriggerType(trigger_type_e::TT_ONE);
	engineConfiguration->camInputs[0] = Gpio::A10; // we just need to indicate that we have CAM

	ASSERT_EQ(0, round(Sensor::getOrZero(SensorType::Rpm))) << "testCamInput RPM";

	for (int i = 0; i < 5; i++) {
		eth.fireRise(25);
		eth.fireFall(25);
	}

	ASSERT_EQ(1200, round(Sensor::getOrZero(SensorType::Rpm)));
	ASSERT_EQ(0, unitTestWarningCodeState.recentWarnings.getCount()) << "warningCounter#testCamInput";

	for (int i = 0; i < 600; i++) {
		eth.fireRise(25);
		eth.fireFall(25);
	}

	for (int i = 0; i < 600; i++) {
		eth.moveTimeForwardUs(MS2US(10));

		// cam comes every other crank rev
		if (i % 2 == 0) {
			hwHandleVvtCamSignal(true, getTimeNowNt(), 0);
		}

		eth.moveTimeForwardUs(MS2US(15));

		eth.firePrimaryTriggerRise();
		EXPECT_EQ(1200, round(Sensor::getOrZero(SensorType::Rpm)));

		eth.moveTimeForwardUs(MS2US(25));
		eth.firePrimaryTriggerFall();
	}

	// asserting that error code has cleared
	ASSERT_EQ(0, unitTestWarningCodeState.recentWarnings.getCount()) << "warningCounter#testCamInput #3";
	EXPECT_NEAR_M3(108, engine->triggerCentral.getVVTPosition(0, 0).value_or(0));
}

TEST(trigger, testNB2CamInput) {
	EngineTestHelper eth(engine_type_e::FRANKENSO_MAZDA_MIATA_2003);

	engineConfiguration->isFasterEngineSpinUpEnabled = false;

	ASSERT_EQ(0, round(Sensor::getOrZero(SensorType::Rpm)));
	for (int i = 0; i < 6; i++) {
		eth.fireRise(25 * 70 / 180);
		eth.fireRise(25 * 110 / 180);
		ASSERT_EQ(0, round(Sensor::getOrZero(SensorType::Rpm)));
	}
	eth.fireRise(25 * 70 / 180);
	eth.fireRise(25 * 110 / 180);
	// first time we have RPM
	ASSERT_EQ(1250, round(Sensor::getOrZero(SensorType::Rpm)));

	int totalRevolutionCountBeforeVvtSync = 5;
	// need to be out of VVT sync to see VVT sync in action
	eth.fireRise(25 * 70 / 180);
	eth.fireRise(25 * 110 / 180);
	ASSERT_EQ(totalRevolutionCountBeforeVvtSync, engine->triggerCentral.triggerState.getCrankSynchronizationCounter());
	ASSERT_TRUE((totalRevolutionCountBeforeVvtSync % SYMMETRICAL_CRANK_SENSOR_DIVIDER) != 0);

	eth.moveTimeForwardUs(MS2US(3)); // shifting VVT phase a few angles

	hwHandleVvtCamSignal(true, getTimeNowNt(), 0);

	// first gap - long

	eth.moveTimeForwardUs(MS2US(130));
	hwHandleVvtCamSignal(false, getTimeNowNt(), 0);
	eth.moveTimeForwardUs(MS2US(30));
	hwHandleVvtCamSignal(true, getTimeNowNt(), 0);

	// second gap - short

	eth.moveTimeForwardUs(MS2US(10));
	hwHandleVvtCamSignal(false, getTimeNowNt(), 0);
	eth.moveTimeForwardUs(MS2US(10));
	hwHandleVvtCamSignal(true, getTimeNowNt(), 0);

	ASSERT_FALSE(engine->triggerCentral.getVVTPosition(0, 0));
	ASSERT_EQ(totalRevolutionCountBeforeVvtSync, engine->triggerCentral.triggerState.getCrankSynchronizationCounter());

	// Third gap - long

	eth.moveTimeForwardUs(MS2US(130));
	hwHandleVvtCamSignal(false, getTimeNowNt(), 0);
	eth.moveTimeForwardUs(MS2US(30));
	hwHandleVvtCamSignal(true, getTimeNowNt(), 0);

	EXPECT_NEAR(297.5f, engine->triggerCentral.getVVTPosition(0, 0).value_or(0), EPS2D);
	// actually position based on cam!
	EXPECT_FLOAT_EQ(540, engine->triggerCentral.triggerState.m_phaseAdjustment);
	EXPECT_TRUE(engine->triggerCentral.triggerState.hasSynchronizedPhase());

	EXPECT_EQ(39, waveChart.getSize());
}

// Cam shapes have less room than the primary trigger shape, so check every cam pattern fits
TEST(trigger, everyVvtModeFitsCamShape) {
	for (int i = 0; i < 256; i++) {
		auto mode = static_cast<vvt_mode_e>(i);
		auto name = getVvt_mode_e(mode);
		if (strcmp(name, "unknown") == 0) {
			continue;
		}

		EngineTestHelper eth(engine_type_e::TEST_ENGINE);
		engineConfiguration->vvtMode[0] = mode;
		// Building a shape that doesn't fit is a firmware error, which throws here
		ASSERT_NO_THROW(engine->updateTriggerWaveform()) << name;

		const auto& shape = engine->triggerCentral.vvtShape[0];
		EXPECT_FALSE(shape.shapeDefinitionError) << name;
		EXPECT_LE(shape.getSize(), shape.getCapacity()) << name;
	}
}
