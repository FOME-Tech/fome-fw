#include "pch.h"

#include "frequency_sensor.h"
#include "sensor_type.h"
#include "identity_func.h"

class FrequencySensorTest : public ::testing::Test {
public:
	FrequencySensorTest()
		: dut(SensorType::FuelEthanolPercent, MS2NT(50)) {}

	void SetUp() override {
		// If somehow prodcode will be unwrapped for test it MAYBE! will fire with error.
		// At least we must init FlexSensor somehow
		dut.initIfValid(Gpio::A0, identityFunction, 0.1f);
	}

	/*
	 *  This method must simulate some amount periods of square-wave
	 *  and fire callback on every falling edge.
	 *  (as Sensor works by falling edge)
	 */
	void generatePwm(EngineTestHelper& eth, float freqHz) {
		constexpr auto periods = 1000;
		auto period = (1 / freqHz);

		std::cout << "PERIOD: " << period << std::endl;

		for (auto i = 0; i < periods; i++) {
			eth.moveTimeForwardSec(period);
			dut.onEdge(getTimeNowNt());
		}
	}

	FrequencySensor dut;
};

/*
 *  Sensor must take PWM input on "valid" frequency and generate any input.
 */
TEST_F(FrequencySensorTest, testValidWithPwm) {
	ASSERT_TRUE(dut.Register());
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	// Should be invalid - not set yet
	{
		auto s = Sensor::get(SensorType::FuelEthanolPercent);
		EXPECT_FALSE(s.Valid);
	}

	generatePwm(eth, 10);

	// Should be valid, correct frequency
	{
		auto s = Sensor::get(SensorType::FuelEthanolPercent);
		EXPECT_TRUE(s.Valid);
		EXPECT_NEAR(s.Value, 10, 1e-3);
	}
}

// A burn that leaves the pin alone keeps the sensor; moving or removing the pin releases it
TEST_F(FrequencySensorTest, deInitOnlyWhenPinChanges) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	ASSERT_TRUE(dut.Register());

	dut.deInit(Gpio::A0);
	EXPECT_EQ(&dut, Sensor::getSensorOfType(SensorType::FuelEthanolPercent));

	// Still on the same pin, nothing to restart
	dut.initIfValid(Gpio::A0, identityFunction, 0.1f);
	EXPECT_EQ(&dut, Sensor::getSensorOfType(SensorType::FuelEthanolPercent));

	dut.deInit(Gpio::Unassigned);
	EXPECT_EQ(nullptr, Sensor::getSensorOfType(SensorType::FuelEthanolPercent));
}
