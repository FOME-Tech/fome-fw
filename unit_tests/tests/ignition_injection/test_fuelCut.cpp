/*
 * @file test_fuelCut.cpp
 *
 * @date Mar 22, 2018
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"

#include "event_queue.h"

using ::testing::_;

// Define some helpers for not-cut and cut
#define EXPECT_NORMAL() EXPECT_FLOAT_EQ(normalInjMass, engine->cylinders[0].getInjectionMass())
#define EXPECT_CUT() EXPECT_FLOAT_EQ(0, engine->cylinders[0].getInjectionMass())

TEST(fuelCut, coasting) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	EXPECT_CALL(*eth.mockAirmass, getAirmass(_, _))
		.WillRepeatedly(Return(AirmassResult{1.0f, 50.0f}));
	engineConfiguration->stoichRatioPrimary = 10;

	// configure coastingFuelCut
	engineConfiguration->coastingFuelCutEnabled = true;
	engineConfiguration->coastingFuelCutRpmLow = 1300;
	engineConfiguration->coastingFuelCutRpmHigh = 1500;
	engineConfiguration->coastingFuelCutTps = 2;
	engineConfiguration->coastingFuelCutClt = 30;
	engineConfiguration->coastingFuelCutMap = 100;
	// set cranking threshold
	engineConfiguration->cranking.rpm = 999;

	// basic engine setup
	setupSimpleTestEngineWithMafAndTT_ONE_trigger(&eth);

	Sensor::setMockValue(SensorType::Map, 0);

	// mock CLT - just above threshold ('hot engine')
	float hotClt = engineConfiguration->coastingFuelCutClt + 1;
	Sensor::setMockValue(SensorType::Clt, hotClt);
	// mock TPS - throttle is opened
	Sensor::setMockValue(SensorType::DriverThrottleIntent, 60);
	// set 'running' RPM - just above RpmHigh threshold
	Sensor::setMockValue(SensorType::Rpm, engineConfiguration->coastingFuelCutRpmHigh + 1);
	// 'advance' time (amount doesn't matter)
	eth.moveTimeForwardUs(1000);

	const float normalInjMass = 0.1f;
	/*
	 * We need to pass through all rpm changes (high-mid-low-mid-high) because of state-machine
	 */

	// process
	eth.engine.periodicFastCallback();

	// this is normal injection mode (the throttle is opened), no fuel cut-off
	EXPECT_NORMAL();

	// 'releasing' the throttle
	Sensor::setMockValue(SensorType::DriverThrottleIntent, 0);
	eth.engine.periodicFastCallback();

	// Fuel cut-off is enabled now
	EXPECT_CUT();

	// Now drop the CLT below threshold
	Sensor::setMockValue(SensorType::Clt, engineConfiguration->coastingFuelCutClt - 1);
	eth.engine.periodicFastCallback();

	// Fuel cut-off should be diactivated - the engine is 'cold'
	EXPECT_NORMAL();

	// restore CLT
	Sensor::setMockValue(SensorType::Clt, hotClt);
	// And set RPM - somewhere between RpmHigh and RpmLow threshold
	Sensor::setMockValue(SensorType::Rpm, (engineConfiguration->coastingFuelCutRpmHigh + engineConfiguration->coastingFuelCutRpmLow) / 2);
	eth.engine.periodicFastCallback();

	// Fuel cut-off is enabled - nothing should change
	EXPECT_NORMAL();

	// Now drop RPM just below RpmLow threshold
	Sensor::setMockValue(SensorType::Rpm, engineConfiguration->coastingFuelCutRpmLow - 1);
	eth.engine.periodicFastCallback();

	// Fuel cut-off is now disabled (the engine is idling)
	EXPECT_NORMAL();

	// Now change RPM just below RpmHigh threshold
	Sensor::setMockValue(SensorType::Rpm, engineConfiguration->coastingFuelCutRpmHigh - 1);
	eth.engine.periodicFastCallback();

	// Fuel cut-off is still disabled
	EXPECT_NORMAL();

	// Now set RPM just above RpmHigh threshold
	Sensor::setMockValue(SensorType::Rpm, engineConfiguration->coastingFuelCutRpmHigh + 1);
	eth.engine.periodicFastCallback();

	// Fuel cut-off is active again!
	EXPECT_CUT();

	// Configure vehicle speed thresholds
	engineConfiguration->coastingFuelCutVssHigh = 50;
	engineConfiguration->coastingFuelCutVssLow = 40;

	// High speed, should still be cut.
	Sensor::setMockValue(SensorType::VehicleSpeed, 55);
	eth.engine.periodicFastCallback();
	EXPECT_CUT();

	// Between thresholds, still cut.
	Sensor::setMockValue(SensorType::VehicleSpeed, 45);
	eth.engine.periodicFastCallback();
	EXPECT_CUT();

	// Below lower threshold, normal fuel resumes
	Sensor::setMockValue(SensorType::VehicleSpeed, 35);
	eth.engine.periodicFastCallback();
	EXPECT_NORMAL();

	// Between thresholds, still normal.
	Sensor::setMockValue(SensorType::VehicleSpeed, 45);
	eth.engine.periodicFastCallback();
	EXPECT_NORMAL();

	// Back above upper, cut again.
	Sensor::setMockValue(SensorType::VehicleSpeed, 55);
	eth.engine.periodicFastCallback();
	EXPECT_CUT();
}

TEST(fuelCut, delay) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	EXPECT_CALL(*eth.mockAirmass, getAirmass(_, _))
		.WillRepeatedly(Return(AirmassResult{1.0f, 50.0f}));
	engineConfiguration->stoichRatioPrimary = 10;

	// configure coastingFuelCut
	engineConfiguration->coastingFuelCutEnabled = true;
	engineConfiguration->coastingFuelCutRpmLow = 1300;
	engineConfiguration->coastingFuelCutRpmHigh = 1500;
	engineConfiguration->coastingFuelCutTps = 2;
	engineConfiguration->coastingFuelCutClt = 30;
	engineConfiguration->coastingFuelCutMap = 100;
	// set cranking threshold
	engineConfiguration->cranking.rpm = 999;

	// delay is 1 second
	engineConfiguration->dfcoDelay = 1.0f;

	// basic engine setup
	setupSimpleTestEngineWithMafAndTT_ONE_trigger(&eth);

	Sensor::setMockValue(SensorType::Map, 0);

	// mock CLT - just above threshold ('hot engine')
	float hotClt = engineConfiguration->coastingFuelCutClt + 1;
	Sensor::setMockValue(SensorType::Clt, hotClt);
	// mock TPS - throttle is opened
	Sensor::setMockValue(SensorType::DriverThrottleIntent, 60);
	// set 'running' RPM - just above RpmHigh threshold
	Sensor::setMockValue(SensorType::Rpm, engineConfiguration->coastingFuelCutRpmHigh + 1);
	// 'advance' time (amount doesn't matter)
	eth.moveTimeForwardUs(1000);

	const float normalInjMass = 0.1f;

	setTimeNowUs(1e6);

	// process
	eth.engine.periodicFastCallback();

	// this is normal injection mode (the throttle is opened), no fuel cut-off
	EXPECT_NORMAL();

	// 'releasing' the throttle
	Sensor::setMockValue(SensorType::DriverThrottleIntent, 0);
	eth.engine.periodicFastCallback();

	// Shouldn't cut yet, since not enough time has elapsed
	EXPECT_NORMAL();

	// Change nothing else, but advance time and update again
	advanceTimeUs(0.9e6);
	eth.engine.periodicFastCallback();

	// too soon, still no cut
	EXPECT_NORMAL();

	// Change nothing else, but advance time and update again
	advanceTimeUs(0.2e6);
	eth.engine.periodicFastCallback();

	// Should now be cut!
	EXPECT_CUT();

	// Put the throtle back, and it should come back instantly
	Sensor::setMockValue(SensorType::DriverThrottleIntent, 30);
	eth.engine.periodicFastCallback();
	EXPECT_NORMAL();
}

TEST(fuelCut, mapTable) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	EXPECT_CALL(*eth.mockAirmass, getAirmass(_, _))
		.WillRepeatedly(Return(AirmassResult{1.0f, 50.0f}));
	engineConfiguration->stoichRatioPrimary = 10;

	// configure coastingFuelCut
	engineConfiguration->coastingFuelCutEnabled = true;
	engineConfiguration->coastingFuelCutRpmLow = 1300;
	engineConfiguration->coastingFuelCutRpmHigh = 1500;
	engineConfiguration->coastingFuelCutTps = 2;
	engineConfiguration->coastingFuelCutClt = 30;
	engineConfiguration->dfcoDelay = 0;
	engineConfiguration->coastingFuelCutMap = 10;
	// set cranking threshold
	engineConfiguration->cranking.rpm = 999;

	//setup MAP cutoff table
	engineConfiguration->useTableForDfcoMap = true;
	copyArray(config->dfcoMapRpmValuesBins, { 2000, 3000, 4000, 5000 });
	copyArray(config->dfcoMapRpmValues, { 50, 30, 20, 10 });

	// basic engine setup
	setupSimpleTestEngineWithMafAndTT_ONE_trigger(&eth);

	// set MAP above all values in the table
	Sensor::setMockValue(SensorType::Map, 60);
	// mock CLT - just above threshold ('hot engine')
	Sensor::setMockValue(SensorType::Clt, engineConfiguration->coastingFuelCutClt + 1);
	// mock TPS - throttle is opened
	Sensor::setMockValue(SensorType::DriverThrottleIntent, 0);
	// set 'running' RPM in the middle of two interpolation values
	Sensor::setMockValue(SensorType::Rpm, 2500);
	// 'advance' time (amount doesn't matter)
	eth.moveTimeForwardUs(1000);

	const float normalInjMass = 0.1f;

	// MAP > threshold, expect fueling
	eth.engine.periodicFastCallback();
	EXPECT_NORMAL();

	// Drop MAP to just above the interpolated cutoff curve
	Sensor::setMockValue(SensorType::Map, 43);
	eth.engine.periodicFastCallback();
	EXPECT_NORMAL();

	// Drop MAP to just below the interpolated cutoff curve
	Sensor::setMockValue(SensorType::Map, 37);
	eth.engine.periodicFastCallback();
	EXPECT_CUT();

	// icrease RPM such that we move above the cutoff curve
	Sensor::setMockValue(SensorType::Rpm, 4000);
	eth.engine.periodicFastCallback();
	EXPECT_NORMAL();
}
// testing the ablity to have clutch input disable fuel cut when configured
TEST(fuelCut, clutch) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	EXPECT_CALL(*eth.mockAirmass, getAirmass(_, _))
		.WillRepeatedly(Return(AirmassResult{1.0f, 50.0f}));
	engineConfiguration->stoichRatioPrimary = 10;

	// configure coastingFuelCut
	engineConfiguration->coastingFuelCutEnabled = true;
	engineConfiguration->coastingFuelCutRpmLow = 1300;
	engineConfiguration->coastingFuelCutRpmHigh = 1500;
	engineConfiguration->coastingFuelCutTps = 2;
	engineConfiguration->coastingFuelCutClt = 30;
	engineConfiguration->coastingFuelCutMap = 100;
	// set cranking threshold
	engineConfiguration->cranking.rpm = 999;

	// basic engine setup
	setupSimpleTestEngineWithMafAndTT_ONE_trigger(&eth);
	
	//all other conditions should allow fuel cut
	float hotClt = engineConfiguration->coastingFuelCutClt + 1;
	Sensor::setMockValue(SensorType::Clt, hotClt);
	Sensor::setMockValue(SensorType::DriverThrottleIntent, 0);
	Sensor::setMockValue(SensorType::Rpm, engineConfiguration->coastingFuelCutRpmHigh + 1);
	Sensor::setMockValue(SensorType::Map, 0);
	eth.moveTimeForwardUs(1000);

	const float normalInjMass = 0.1f;
	eth.engine.periodicFastCallback();

	// feature disabled, all fuel cut conditions met
	engineConfiguration->disableFuelCutOnClutch = false;
	setMockState(engineConfiguration->clutchUpPin, false);
	engine->updateSwitchInputs();
	eth.engine.periodicFastCallback();
	EXPECT_CUT();

	// feature enabled, io not configured, fuelCut should still occur
	engineConfiguration->disableFuelCutOnClutch = true;
	setMockState(engineConfiguration->clutchUpPin, false);
	engine->updateSwitchInputs();
	eth.engine.periodicFastCallback();
	EXPECT_CUT();

 	//configure io, fuelCut should now be inhibited
	engineConfiguration->clutchUpPin = Gpio::G2;
	engineConfiguration->clutchUpPinMode = PI_PULLDOWN;
	setMockState(engineConfiguration->clutchUpPin, false);
	engine->updateSwitchInputs();
	eth.engine.periodicFastCallback();
	EXPECT_NORMAL();

	// release clutch, fuel cut should now happen
	setMockState(engineConfiguration->clutchUpPin, true);
	engine->updateSwitchInputs();
	eth.engine.periodicFastCallback();
	EXPECT_CUT();
}