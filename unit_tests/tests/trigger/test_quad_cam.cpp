/**
 * @file test_quad_cam.cpp
 *
 */

#include "pch.h"

TEST(trigger, testQuadCam) {
	// setting some weird engine
	EngineTestHelper eth(engine_type_e::FORD_ESCORT_GT);
	engineConfiguration->isFasterEngineSpinUpEnabled = false;
	engineConfiguration->alwaysInstantRpm = true;

	setCrankOperationMode();

	// changing to 'ONE TOOTH' trigger on CRANK with CAM/VVT
	engineConfiguration->vvtMode[0] = VVT_SINGLE_TOOTH;
	engineConfiguration->vvtMode[1] = VVT_SINGLE_TOOTH;

	engineConfiguration->camInputs[0] = Gpio::A10; // we just need to indicate that we have CAM

	// this crank trigger would be easier to test, crank shape is less important for this test
	eth.setTriggerType(trigger_type_e::TT_ONE);

	ASSERT_EQ(0, Sensor::getOrZero(SensorType::Rpm));

	eth.fireFall(12.5);
	eth.fireRise(12.5);
	ASSERT_EQ( 0,  Sensor::getOrZero(SensorType::Rpm));

	eth.fireFall(12.5);
	eth.fireRise(12.5);
	// first time we have RPM
	ASSERT_EQ(2400, Sensor::getOrZero(SensorType::Rpm));

	// need to be out of VVT sync to see VVT sync in action
	eth.fireFall(12.5);
	eth.fireRise(12.5);
	eth.fireFall(12.5);
	eth.fireRise(12.5);
	eth.fireFall(12.5);
	eth.fireRise(12.5);

	eth.moveTimeForwardUs(MS2US(3)); // shifting VVT phase a few angles

	float d = 4;

	int firstCam = 0;
	int secondCam = 1;

	int firstBank = 0;
	int secondBank = 1;

	int firstCamSecondBank = secondBank * CAMS_PER_BANK + firstCam;
	int secondCamSecondBank = secondBank * CAMS_PER_BANK + secondCam;

	// Cams should have no position yet
	ASSERT_EQ(0, engine->triggerCentral.getVVTPosition(firstBank, firstCam));
	ASSERT_EQ(0, engine->triggerCentral.getVVTPosition(firstBank, secondCam));
	ASSERT_EQ(0, engine->triggerCentral.getVVTPosition(secondBank, firstCam));
	ASSERT_EQ(0, engine->triggerCentral.getVVTPosition(secondBank, secondCam));

	hwHandleVvtCamSignal(true, getTimeNowNt(), firstCam);
	hwHandleVvtCamSignal(true, getTimeNowNt(), secondCam);
	hwHandleVvtCamSignal(true, getTimeNowNt(), firstCamSecondBank);
	hwHandleVvtCamSignal(true, getTimeNowNt(), secondCamSecondBank);

	float basePos = -80.2f;

	// All four cams should now have the same position
	EXPECT_NEAR_M3(basePos, engine->triggerCentral.getVVTPosition(firstBank, firstCam));
	EXPECT_NEAR_M3(basePos, engine->triggerCentral.getVVTPosition(firstBank, secondCam));
	EXPECT_NEAR_M3(basePos, engine->triggerCentral.getVVTPosition(secondBank, firstCam));
	EXPECT_NEAR_M3(basePos, engine->triggerCentral.getVVTPosition(secondBank, secondCam));

	// Now fire cam events again, but with time gaps between each
	eth.moveTimeForwardMs(1);
	hwHandleVvtCamSignal(true, getTimeNowNt(), firstCam);
	eth.moveTimeForwardMs(1);
	hwHandleVvtCamSignal(true, getTimeNowNt(), secondCam);
	eth.moveTimeForwardMs(1);
	hwHandleVvtCamSignal(true, getTimeNowNt(), firstCamSecondBank);
	eth.moveTimeForwardMs(1);
	hwHandleVvtCamSignal(true, getTimeNowNt(), secondCamSecondBank);

	// All four cams should have different positions, each retarded by 1ms from the last
	float oneMsDegrees = 1000 / engine->rpmCalculator.oneDegreeUs;
	EXPECT_NEAR(basePos - oneMsDegrees * 1, engine->triggerCentral.getVVTPosition(firstBank, firstCam), EPS3D);
	EXPECT_NEAR(basePos - oneMsDegrees * 2, engine->triggerCentral.getVVTPosition(firstBank, secondCam), EPS3D);
	EXPECT_NEAR(basePos - oneMsDegrees * 3, engine->triggerCentral.getVVTPosition(secondBank, firstCam), EPS3D);
	EXPECT_NEAR(basePos - oneMsDegrees * 4, engine->triggerCentral.getVVTPosition(secondBank, secondCam), EPS3D);
}
