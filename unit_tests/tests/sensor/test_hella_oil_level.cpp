#include "pch.h"

#include "hella_oil_level.h"

// The sensor emits a 1000ms frame:
//   T1 (temperature) -> 110ms -> T2 (level) -> 110ms -> T3 (diag, 68.2ms) -> 780ms -> repeat
// Pulse width runs 22ms (bottom of range) to 88ms (top of range). All times are rising edge
// to rising edge, so a pulse "slot" is 110ms regardless of how wide the pulse itself is.
static constexpr float slotMs = 110;
static constexpr float diagPulseMs = 68.2;
static constexpr float breakMs = 780;

class HellaOilLevelTest : public ::testing::Test {
public:
	HellaOilLevelTest()
		: dut(SensorType::OilLevel, SensorType::OilTemperature) {}

	// Feed one pulse: rising edge, hold high for widthMs, falling edge.
	// Then idle until the next rising edge is slotMs after this one.
	void pulse(float widthMs, float untilNextRiseMs) {
		dut.onEdge(getTimeNowNt(), true);
		advanceMs(widthMs);
		dut.onEdge(getTimeNowNt(), false);
		advanceMs(untilNextRiseMs - widthMs);
	}

	// One complete frame with the given temperature and level pulse widths
	void frame(float tempPulseMs, float levelPulseMs) {
		pulse(tempPulseMs, slotMs);
		pulse(levelPulseMs, slotMs);
		pulse(diagPulseMs, breakMs);
	}

	void advanceMs(float ms) {
		m_eth.moveTimeForwardSec(ms / 1000);
	}

	// Each test registers a fresh sensor into the global registry, so clear it out
	// explicitly rather than relying on member destruction order.
	void TearDown() override {
		Sensor::resetRegistry();
	}

	EngineTestHelper m_eth{engine_type_e::TEST_ENGINE};
	HellaOilLevelSensor dut;
};

// 45mm variant (6PR 009 622-041) reads 20.9mm at 22ms and 65.9mm at 88ms
TEST_F(HellaOilLevelTest, level45mmVariant) {
	dut.init(Gpio::A0, HELLA_OIL_LEVEL_45MM);

	// Nothing decoded yet - we haven't acquired sync
	EXPECT_FALSE(Sensor::get(SensorType::OilLevel).Valid);

	// First frame acquires sync on the break, so the level pulse of the *second* frame
	// is the first one we can decode.
	frame(55, 55);

	frame(55, 22);
	EXPECT_NEAR(Sensor::get(SensorType::OilLevel).value_or(-1), 20.9f, 0.1f);

	frame(55, 88);
	EXPECT_NEAR(Sensor::get(SensorType::OilLevel).value_or(-1), 65.9f, 0.1f);

	// Midpoint: 55ms is halfway between 22 and 88, so halfway between 20.9 and 65.9
	frame(55, 55);
	EXPECT_NEAR(Sensor::get(SensorType::OilLevel).value_or(-1), 43.4f, 0.1f);
}

// 129mm variant (6PR 009 622-051) reads 18.0mm at 22ms and 147.0mm at 88ms
TEST_F(HellaOilLevelTest, level129mmVariant) {
	dut.init(Gpio::A0, HELLA_OIL_LEVEL_129MM);

	frame(55, 55);

	frame(55, 22);
	EXPECT_NEAR(Sensor::get(SensorType::OilLevel).value_or(-1), 18.0f, 0.1f);

	frame(55, 88);
	EXPECT_NEAR(Sensor::get(SensorType::OilLevel).value_or(-1), 147.0f, 0.1f);
}

// Temperature spans -40C to 160C for every variant
TEST_F(HellaOilLevelTest, temperature) {
	dut.init(Gpio::A0, HELLA_OIL_LEVEL_45MM);

	frame(55, 55);

	frame(22, 55);
	EXPECT_NEAR(Sensor::get(SensorType::OilTemperature).value_or(0), -40.0f, 0.1f);

	frame(88, 55);
	EXPECT_NEAR(Sensor::get(SensorType::OilTemperature).value_or(0), 160.0f, 0.1f);

	// Midpoint of 22..88 is 55ms, midpoint of -40..160 is 60C
	frame(55, 55);
	EXPECT_NEAR(Sensor::get(SensorType::OilTemperature).value_or(0), 60.0f, 0.1f);
}

// Level and temperature must not be confused with one another
TEST_F(HellaOilLevelTest, decodesBothChannelsIndependently) {
	dut.init(Gpio::A0, HELLA_OIL_LEVEL_45MM);

	frame(55, 55);
	frame(22, 88);

	EXPECT_NEAR(Sensor::get(SensorType::OilTemperature).value_or(0), -40.0f, 0.1f);
	EXPECT_NEAR(Sensor::get(SensorType::OilLevel).value_or(-1), 65.9f, 0.1f);
}

// Out-of-range pulse widths mean we lost the plot - drop sync rather than report garbage
TEST_F(HellaOilLevelTest, implausiblePulseDropsSync) {
	dut.init(Gpio::A0, HELLA_OIL_LEVEL_45MM);

	frame(55, 55);
	frame(55, 22);
	EXPECT_NEAR(Sensor::get(SensorType::OilLevel).value_or(-1), 20.9f, 0.1f);

	// A 5ms pulse is impossibly short. It resets the state machine, so the level pulse
	// that follows in this frame is not trusted and the reading must not update.
	pulse(5, slotMs);
	pulse(88, slotMs);
	pulse(diagPulseMs, breakMs);

	// Still the old value, not the 65.9mm that an 88ms pulse would have decoded to
	EXPECT_NEAR(Sensor::get(SensorType::OilLevel).value_or(-1), 20.9f, 0.1f);
}

// Signal loss must invalidate rather than latch the last reading forever
TEST_F(HellaOilLevelTest, timesOutWhenSignalStops) {
	dut.init(Gpio::A0, HELLA_OIL_LEVEL_45MM);

	frame(55, 55);
	frame(55, 55);
	EXPECT_TRUE(Sensor::get(SensorType::OilLevel).Valid);
	EXPECT_TRUE(Sensor::get(SensorType::OilTemperature).Valid);

	// Sensor updates once per second, and the timeout is 2 seconds
	advanceMs(2500);

	EXPECT_FALSE(Sensor::get(SensorType::OilLevel).Valid);
	EXPECT_FALSE(Sensor::get(SensorType::OilTemperature).Valid);
}

// An analog oil temperature sender takes priority - registering twice is a firmware error
TEST_F(HellaOilLevelTest, doesNotStealTemperatureFromAnalogSender) {
	StoredValueSensor analogOilTemp(SensorType::OilTemperature, MS2NT(1000));
	ASSERT_TRUE(analogOilTemp.Register());

	dut.init(Gpio::A0, HELLA_OIL_LEVEL_45MM);

	// Level is ours, temperature belongs to the analog sender
	frame(55, 55);
	frame(88, 22);

	EXPECT_NEAR(Sensor::get(SensorType::OilLevel).value_or(-1), 20.9f, 0.1f);

	// The analog sender still owns this channel, so our 88ms pulse (160C) was not published
	analogOilTemp.setValidValue(75, getTimeNowNt());
	EXPECT_NEAR(Sensor::get(SensorType::OilTemperature).value_or(0), 75.0f, 0.1f);

	// Tearing down must not evict the analog sender
	dut.deInit();
	EXPECT_TRUE(Sensor::hasSensor(SensorType::OilTemperature));

	analogOilTemp.unregister();
}
