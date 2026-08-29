#include "pch.h"
#include "flex_sensor.h"

// 100hz = E50
static constexpr float e50Hz = 100;

TEST(FlexConverter, NoRampUpOnFirstReading) {
	FlexConverter dut;

	// The very first reading must be reported as-is: the filter starts out at the value it sees,
	// rather than ramping up towards it from zero over the first second
	EXPECT_NEAR(50, dut.convert(e50Hz).value_or(-1), 1e-3);

	// Steady state stays put
	for (size_t i = 0; i < 100; i++) {
		EXPECT_NEAR(50, dut.convert(e50Hz).value_or(-1), 1e-3);
	}
}

TEST(FlexConverter, PreloadFilter) {
	FlexConverter dut;

	// Last time we ran, the tank had E80 in it
	dut.preloadFilter(80);

	// Sensor now reads E50 - we start from the preloaded value and filter towards the new one
	float first = dut.convert(e50Hz).value_or(-1);
	EXPECT_GT(first, 79);
	EXPECT_LE(first, 80);

	// ~1 second of readings (100hz sensor) is enough to get there
	for (size_t i = 0; i < 100; i++) {
		dut.convert(e50Hz);
	}

	EXPECT_NEAR(50, dut.convert(e50Hz).value_or(-1), 0.1f);
}

TEST(FlexConverter, OutOfRange) {
	FlexConverter dut;

	EXPECT_EQ(UnexpectedCode::Low, dut.convert(40).Code);
	EXPECT_EQ(UnexpectedCode::High, dut.convert(200).Code);
}
