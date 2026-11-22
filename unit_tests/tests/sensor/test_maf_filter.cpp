#include "pch.h"

#include "sensor/maf_filter.h"

// Feed a step into the filter for one second's worth of ADC updates
static float runStepForOneSecond(MafFilter& dut, float rpm, float input) {
	Sensor::setMockValue(SensorType::Rpm, rpm);

	float result = 0;
	for (int i = 0; i < hzForPeriod(ADC_UPDATE_RATE); i++) {
		result = dut.convert(input).value_or(-1);
	}

	return result;
}

TEST(MafFilter, TimeConstantIsParameterOverRpm) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->mafFilterParameter = 600;

	MafFilter dut;

	// Engine stopped: filter tracks the input exactly
	Sensor::setMockValue(SensorType::Rpm, 0);
	EXPECT_EQ(0, dut.convert(0).value_or(-1));

	// 600 / 600 rpm = 1 second time constant, so after 1 second a step gets 1 - 1/e of the way
	EXPECT_NEAR(100 * (1 - expf(-1)), runStepForOneSecond(dut, 600, 100), 0.5f);
}

TEST(MafFilter, TimeConstantScalesWithRpm) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->mafFilterParameter = 600;

	MafFilter dut;
	Sensor::setMockValue(SensorType::Rpm, 0);
	dut.convert(0);

	// 600 / 300 rpm = 2 second time constant
	EXPECT_NEAR(100 * (1 - expf(-0.5f)), runStepForOneSecond(dut, 300, 100), 0.5f);
}

TEST(MafFilter, ZeroParameterDisablesFilter) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->mafFilterParameter = 0;

	MafFilter dut;
	Sensor::setMockValue(SensorType::Rpm, 600);
	dut.convert(0);

	EXPECT_EQ(100, dut.convert(100).value_or(-1));
}
