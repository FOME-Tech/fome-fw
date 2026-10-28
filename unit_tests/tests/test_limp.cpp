#include "pch.h"

#include "limp_manager.h"

TEST(limp, testFatalError) {
	LimpManager dut;

	// Everything should work by default
	ASSERT_TRUE(dut.allowElectronicThrottle());
	ASSERT_TRUE(dut.allowIgnition());
	ASSERT_TRUE(dut.allowInjection());
	ASSERT_TRUE(dut.allowTriggerInput());

	dut.fatalError();

	// Fatal error should kill everything
	EXPECT_FALSE(dut.allowElectronicThrottle());
	EXPECT_FALSE(dut.allowIgnition());
	EXPECT_FALSE(dut.allowInjection());
	EXPECT_FALSE(dut.allowTriggerInput());
}

TEST(limp, revLimit) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	engineConfiguration->rpmHardLimit = 2500;
	engineConfiguration->cutFuelOnHardLimit = true;
	engineConfiguration->cutSparkOnHardLimit = true;

	LimpManager dut;

	// Under rev limit, inj/ign allowed
	dut.updateState(2000, 0);
	EXPECT_TRUE(dut.allowIgnition());
	EXPECT_TRUE(dut.allowInjection());

	// Over rev limit, no injection
	dut.updateState(3000, 0);
	EXPECT_FALSE(dut.allowIgnition());
	EXPECT_FALSE(dut.allowInjection());

	// Now recover back to under limit
	dut.updateState(2000, 0);
	EXPECT_TRUE(dut.allowIgnition());
	EXPECT_TRUE(dut.allowInjection());
}

TEST(limp, revLimitCltBased) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	engineConfiguration->rpmHardLimit = 2500;
	engineConfiguration->cutFuelOnHardLimit = true;
	engineConfiguration->cutSparkOnHardLimit = true;

	// Configure CLT-based rev limit curve
	engineConfiguration->useCltBasedRpmLimit = true;
	copyArray(config->cltRevLimitRpmBins, {10, 20, 30, 40});
	copyArray(config->cltRevLimitRpm, {1000, 2000, 3000, 4000});

	LimpManager dut;

	// Check low temperature first
	Sensor::setMockValue(SensorType::Clt, 10);

	// Under rev limit, inj/ign allowed
	dut.updateState(900, 0);
	EXPECT_TRUE(dut.allowIgnition());
	EXPECT_TRUE(dut.allowInjection());

	// Over rev limit, no injection
	dut.updateState(1100, 0);
	EXPECT_FALSE(dut.allowIgnition());
	EXPECT_FALSE(dut.allowInjection());

	// Now recover back to under limit
	dut.updateState(900, 0);
	EXPECT_TRUE(dut.allowIgnition());
	EXPECT_TRUE(dut.allowInjection());

	// Check middle temperature
	Sensor::setMockValue(SensorType::Clt, 35);

	// Under rev limit, inj/ign allowed
	dut.updateState(3400, 0);
	EXPECT_TRUE(dut.allowIgnition());
	EXPECT_TRUE(dut.allowInjection());

	// Over rev limit, no injection
	dut.updateState(3600, 0);
	EXPECT_FALSE(dut.allowIgnition());
	EXPECT_FALSE(dut.allowInjection());

	// Now recover back to under limit
	dut.updateState(3400, 0);
	EXPECT_TRUE(dut.allowIgnition());
	EXPECT_TRUE(dut.allowInjection());
}

TEST(limp, revLimitTable) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->rpmHardLimit = 2500;
	engineConfiguration->rpmHardLimitHyst = 100;
	engineConfiguration->cutFuelOnHardLimit = true;
	engineConfiguration->cutSparkOnHardLimit = true;
	engineConfiguration->revLimitUseTable = true;
	config->revLimitXAxis = GPPWM_OilTemp;
	config->revLimitYAxis = GPPWM_Zero;
	copyArray(config->revLimitXBins, {0, 40, 100, 160});
	copyArray(config->revLimitYBins, {0, 1, 2, 3});
	for (auto& row : config->revLimitTable) {
		copyArray(row, {1000, 3000, 6000, 2000});
	}

	LimpManager dut;
	Sensor::setMockValue(SensorType::OilTemperature, -20);
	dut.updateState(900, 0);
	EXPECT_TRUE(dut.allowIgnition());
	EXPECT_TRUE(dut.allowInjection());
	dut.updateState(1100, 0);
	EXPECT_FALSE(dut.allowIgnition());
	EXPECT_FALSE(dut.allowInjection());
	// Remain cut inside the hysteresis band, then recover below it.
	dut.updateState(950, 0);
	EXPECT_FALSE(dut.allowIgnition());
	EXPECT_FALSE(dut.allowInjection());
	dut.updateState(850, 0);
	EXPECT_TRUE(dut.allowIgnition());
	EXPECT_TRUE(dut.allowInjection());

	// Interpolate to 4500 RPM, replacing the fixed 2500 RPM limit.
	Sensor::setMockValue(SensorType::OilTemperature, 70);
	dut.updateState(4400, 0);
	EXPECT_TRUE(dut.allowIgnition());
	EXPECT_TRUE(dut.allowInjection());
	dut.updateState(4600, 0);
	EXPECT_FALSE(dut.allowIgnition());
	EXPECT_FALSE(dut.allowInjection());

	// High temperatures use the last bin and honor the selected cut outputs.
	Sensor::setMockValue(SensorType::OilTemperature, 180);
	engineConfiguration->cutSparkOnHardLimit = false;
	dut.updateState(2100, 0);
	EXPECT_TRUE(dut.allowIgnition());
	EXPECT_FALSE(dut.allowInjection());
	engineConfiguration->cutSparkOnHardLimit = true;
	engineConfiguration->cutFuelOnHardLimit = false;
	dut.updateState(2100, 0);
	EXPECT_FALSE(dut.allowIgnition());
	EXPECT_TRUE(dut.allowInjection());

	// A missing sensor uses zero degrees, matching the CLT limiter fallback.
	Sensor::resetMockValue(SensorType::OilTemperature);
	dut.updateState(1100, 0);
	EXPECT_FALSE(dut.allowIgnition());

	// Disabling the table restores the fixed limit.
	engineConfiguration->revLimitUseTable = false;
	dut.updateState(2100, 0);
	EXPECT_TRUE(dut.allowIgnition());
}

TEST(limp, revLimitSelectableAxes) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->rpmHardLimit = 2500;
	engineConfiguration->cutFuelOnHardLimit = true;
	engineConfiguration->cutSparkOnHardLimit = true;
	engineConfiguration->revLimitUseTable = true;
	config->revLimitXAxis = GPPWM_EthanolPercent;
	config->revLimitYAxis = GPPWM_Map;
	copyArray(config->revLimitXBins, {0, 50, 75, 100});
	copyArray(config->revLimitYBins, {0, 100, 200, 300});
	for (size_t y = 0; y < efi::size(config->revLimitYBins); y++) {
		for (size_t x = 0; x < efi::size(config->revLimitXBins); x++) {
			config->revLimitTable[y][x] = 2000 + 20 * config->revLimitXBins[x] + 1000 * y;
		}
	}

	LimpManager dut;
	Sensor::setMockValue(SensorType::FuelEthanolPercent, 25);
	Sensor::setMockValue(SensorType::Map, 50);
	// Interpolate both axes: 2000 + 500 from ethanol + 500 from MAP.
	dut.updateState(2900, 0);
	EXPECT_TRUE(dut.allowInjection());
	EXPECT_TRUE(dut.allowIgnition());
	EXPECT_EQ(25, engine->outputChannels.revLimitXAxisValue);
	EXPECT_EQ(50, engine->outputChannels.revLimitYAxisValue);
	EXPECT_EQ(3000, engine->outputChannels.revLimitTarget);
	dut.updateState(3100, 0);
	EXPECT_FALSE(dut.allowInjection());
	EXPECT_FALSE(dut.allowIgnition());

	// Select a different X input without modifying the table.
	config->revLimitXAxis = GPPWM_Clt;
	Sensor::setMockValue(SensorType::Clt, 75);
	dut.updateState(3900, 0);
	EXPECT_TRUE(dut.allowInjection());
	EXPECT_TRUE(dut.allowIgnition());
	EXPECT_EQ(75, engine->outputChannels.revLimitXAxisValue);
	EXPECT_EQ(4000, engine->outputChannels.revLimitTarget);

	// An unavailable input falls back to the zero bin.
	Sensor::resetMockValue(SensorType::Clt);
	dut.updateState(2600, 0);
	EXPECT_FALSE(dut.allowInjection());
	EXPECT_FALSE(dut.allowIgnition());
	EXPECT_EQ(0, engine->outputChannels.revLimitXAxisValue);
	EXPECT_EQ(2500, engine->outputChannels.revLimitTarget);
}

TEST(limp, revLimitTableWithClt) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->cutFuelOnHardLimit = true;
	engineConfiguration->cutSparkOnHardLimit = true;
	engineConfiguration->useCltBasedRpmLimit = true;
	engineConfiguration->revLimitUseTable = true;
	copyArray(config->cltRevLimitRpmBins, {0, 40, 80, 120});
	copyArray(config->cltRevLimitRpm, {1000, 3000, 5000, 7000});
	config->revLimitXAxis = GPPWM_OilTemp;
	config->revLimitYAxis = GPPWM_Zero;
	copyArray(config->revLimitXBins, {0, 40, 100, 160});
	copyArray(config->revLimitYBins, {0, 1, 2, 3});
	for (auto& row : config->revLimitTable) {
		copyArray(row, {1000, 3000, 6000, 2000});
	}
	LimpManager dut;

	// CLT is the lower limit.
	Sensor::setMockValue(SensorType::Clt, 40);
	Sensor::setMockValue(SensorType::OilTemperature, 100);
	dut.updateState(3100, 0);
	EXPECT_FALSE(dut.allowIgnition());
	EXPECT_FALSE(dut.allowInjection());
	dut.updateState(2900, 0);
	EXPECT_TRUE(dut.allowIgnition());
	EXPECT_TRUE(dut.allowInjection());

	// Oil temperature is now the lower limit.
	Sensor::setMockValue(SensorType::Clt, 120);
	Sensor::setMockValue(SensorType::OilTemperature, 160);
	dut.updateState(2100, 0);
	EXPECT_FALSE(dut.allowIgnition());
	EXPECT_FALSE(dut.allowInjection());
	dut.updateState(1900, 0);
	EXPECT_TRUE(dut.allowIgnition());
	EXPECT_TRUE(dut.allowInjection());
}

TEST(limp, boostCut) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	// Cut above 100kPa
	engineConfiguration->boostCutPressure = 100;

	LimpManager dut;

	// Below threshold, injection allowed
	Sensor::setMockValue(SensorType::Map, 80);
	dut.updateState(1000, 0);
	EXPECT_TRUE(dut.allowInjection());

	// Above rising threshold, injection cut
	Sensor::setMockValue(SensorType::Map, 105);
	dut.updateState(1000, 0);
	EXPECT_FALSE(dut.allowInjection());

	// Below rising threshold, but should have hysteresis, so not cut yet
	Sensor::setMockValue(SensorType::Map, 95);
	dut.updateState(1000, 0);
	EXPECT_FALSE(dut.allowInjection());

	// Below falling threshold, fuel restored
	Sensor::setMockValue(SensorType::Map, 79);
	dut.updateState(1000, 0);
	EXPECT_TRUE(dut.allowInjection());

	// SPECIAL CASE: threshold of 0 means never boost cut
	engineConfiguration->boostCutPressure = 0;
	Sensor::setMockValue(SensorType::Map, 500);
	dut.updateState(1000, 0);
	EXPECT_TRUE(dut.allowInjection());
}

TEST(limp, boostCutTable) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	// Fixed pressure is ignored in table mode
	engineConfiguration->boostCutPressure = 0;
	engineConfiguration->boostCutUseTable = true;
	engineConfiguration->boostCutXAxis = GPPWM_EthanolPercent;
	engineConfiguration->boostCutYAxis = GPPWM_Zero;

	// Cut at 200kPa on E0, 300kPa on E100
	copyArray(config->boostCutXBins, {0, 50, 75, 100});
	copyArray(config->boostCutYBins, {0, 1, 2, 3});
	for (size_t y = 0; y < efi::size(config->boostCutYBins); y++) {
		copyArray(config->boostCutTable[y], {200, 250, 275, 300});
	}

	LimpManager dut;

	// E0: 210kPa cuts
	Sensor::setMockValue(SensorType::FuelEthanolPercent, 0);
	Sensor::setMockValue(SensorType::Map, 210);
	dut.updateState(1000, 0);
	EXPECT_FALSE(dut.allowInjection());
	EXPECT_EQ(200, engine->outputChannels.boostCutTarget);

	// Below falling threshold, fuel restored
	Sensor::setMockValue(SensorType::Map, 175);
	dut.updateState(1000, 0);
	EXPECT_TRUE(dut.allowInjection());

	// E100: 290kPa is fine
	Sensor::setMockValue(SensorType::FuelEthanolPercent, 100);
	Sensor::setMockValue(SensorType::Map, 290);
	dut.updateState(1000, 0);
	EXPECT_TRUE(dut.allowInjection());
	EXPECT_EQ(300, engine->outputChannels.boostCutTarget);

	// E100: 310kPa cuts
	Sensor::setMockValue(SensorType::Map, 310);
	dut.updateState(1000, 0);
	EXPECT_FALSE(dut.allowInjection());
}

TEST(limp, oilPressureStartupFailureCase) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->minOilPressureAfterStart = 200;
	engineConfiguration->oilPressureProtectionStartDelay = 2.0f;

	LimpManager dut;

	// Low oil pressure!
	Sensor::setMockValue(SensorType::OilPressure, 50);

	// Start the engine
	engine->rpmCalculator.setRpmValue(1000);

	// update & check: injection should be allowed
	dut.updateState(1000, getTimeNowNt());
	EXPECT_TRUE(dut.allowInjection());

	// 4.5 seconds later, should still be allowed (even though pressure is low)
	advanceTimeUs(4.5e6);
	dut.updateState(1000, getTimeNowNt());
	EXPECT_TRUE(dut.allowInjection());

	// 1 second later (5.5 since start), injection should cut
	advanceTimeUs(1.0e6);
	dut.updateState(1000, getTimeNowNt());
	ASSERT_FALSE(dut.allowInjection());

	// But then oil pressure arrives!
	// Injection still isn't allowed, since now we're late.
	Sensor::setMockValue(SensorType::OilPressure, 250);
	dut.updateState(1000, getTimeNowNt());
	ASSERT_FALSE(dut.allowInjection());
}

TEST(limp, oilPressureStartupSuccessCase) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->minOilPressureAfterStart = 200;
	engineConfiguration->oilPressureProtectionStartDelay = 2.0f;

	LimpManager dut;

	// Low oil pressure!
	Sensor::setMockValue(SensorType::OilPressure, 50);

	// Start the engine
	engine->rpmCalculator.setRpmValue(1000);

	// update & check: injection should be allowed
	dut.updateState(1000, getTimeNowNt());
	EXPECT_TRUE(dut.allowInjection());

	// 4.5 seconds later, should still be allowed (even though pressure is low)
	advanceTimeUs(4.5e6);
	dut.updateState(1000, getTimeNowNt());
	EXPECT_TRUE(dut.allowInjection());

	// But then oil pressure arrives!
	Sensor::setMockValue(SensorType::OilPressure, 250);
	dut.updateState(1000, getTimeNowNt());
	ASSERT_TRUE(dut.allowInjection());

	// 1 second later (5.5 since start), injection should be allowed since we saw pressure before the timeout
	advanceTimeUs(1.0e6);
	dut.updateState(1000, getTimeNowNt());
	ASSERT_TRUE(dut.allowInjection());

	// Later, we lose oil pressure, but engine should stay running
	advanceTimeUs(10e6);
	Sensor::setMockValue(SensorType::OilPressure, 10);
	dut.updateState(1000, getTimeNowNt());
	ASSERT_TRUE(dut.allowInjection());
}

TEST(limp, oilPressureRunning) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->oilPressureProtectionStartDelay = 2.0f;
	engineConfiguration->enableOilPressureProtect = true;
	engineConfiguration->minimumOilPressureTimeout = 1.0f;
	setArrayValues(config->minimumOilPressureValues, 100);

	LimpManager dut;

	// Oil pressure starts OK
	Sensor::setMockValue(SensorType::OilPressure, 110);

	// Start the engine
	engine->rpmCalculator.setRpmValue(1000);

	// wait 2.1sec to elapse the start delay then update & check: injection should be allowed
	advanceTimeUs(2.1e6);
	dut.updateState(1000, getTimeNowNt());
	EXPECT_TRUE(dut.allowInjection());

	// A long time later, everything should still be OK
	advanceTimeUs(60e6);
	dut.updateState(1000, getTimeNowNt());
	EXPECT_TRUE(dut.allowInjection());

	// Now oil pressure drops below threshold
	Sensor::setMockValue(SensorType::OilPressure, 90);

	// 0.9 second later, injection should continue as timeout isn't hit yet
	advanceTimeUs(0.9e6);
	dut.updateState(1000, getTimeNowNt());
	ASSERT_TRUE(dut.allowInjection());

	// 0.2 second later (1.1s since low pressure starts), injection should cut
	advanceTimeUs(1.0e6);
	dut.updateState(1000, getTimeNowNt());
	ASSERT_FALSE(dut.allowInjection());

	// Oil pressure is restored, and fuel should be restored too
	Sensor::setMockValue(SensorType::OilPressure, 110);
	dut.updateState(1000, getTimeNowNt());
	ASSERT_TRUE(dut.allowInjection());
}
