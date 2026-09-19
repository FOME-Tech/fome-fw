#include "pch.h"

#include "hella_opst.h"

namespace {
constexpr float DiagnosticOk = 256;
constexpr float DiagnosticPressureFault = 384;
constexpr float DiagnosticTemperatureFault = 512;
constexpr float DiagnosticHardwareFault = 640;

class HellaOpsT : public ::testing::Test {
public:
	HellaOpsT()
		: eth(engine_type_e::TEST_ENGINE) {}

	void SetUp() override {
		Sensor::setMockValue(SensorType::BarometricPressure, 100);
		dut.init(Gpio::A0);
	}

	void TearDown() override {
		dut.deInit();
	}

	void sendSymbol(float periodUs, float pulseUs) {
		dut.onEdge(getTimeNowNt(), true);
		eth.moveTimeForwardUs(pulseUs);
		dut.onEdge(getTimeNowNt(), false);
		eth.moveTimeForwardUs(periodUs - pulseUs);
	}

	void sendFrame(float diagnostic, float temperatureC, float absolutePressureBar, float stretch = 1) {
		const float temperaturePulseUs = 19.2f * temperatureC + 896;
		const float pressurePulseUs = 384 * absolutePressureBar - 64;
		sendSymbol(stretch * 1024, stretch * diagnostic);
		sendSymbol(stretch * 4096, stretch * temperaturePulseUs);
		sendSymbol(stretch * 4096, stretch * pressurePulseUs);
	}

	EngineTestHelper eth;
	HellaOpsTSensor dut;
};
} // namespace

TEST_F(HellaOpsT, DecodesTemperatureAndGaugePressure) {
	sendFrame(DiagnosticOk, 20, 2.0f);
	sendFrame(DiagnosticOk, 20, 2.0f);

	auto temperature = Sensor::get(SensorType::OilTemperature);
	ASSERT_TRUE(temperature.Valid);
	EXPECT_NEAR(temperature.Value, 20, 0.5f);

	auto pressure = Sensor::get(SensorType::OilPressure);
	ASSERT_TRUE(pressure.Valid);
	EXPECT_NEAR(pressure.Value, 100, 1);
}

TEST_F(HellaOpsT, CompensatesOscillatorTolerance) {
	sendFrame(DiagnosticOk, 90, 4.5f, 1.1f);
	sendFrame(DiagnosticOk, 90, 4.5f, 1.1f);

	EXPECT_NEAR(Sensor::get(SensorType::OilTemperature).Value, 90, 0.5f);
	EXPECT_NEAR(Sensor::get(SensorType::OilPressure).Value, 350, 1);
}

TEST_F(HellaOpsT, ReportsDiagnosticFaults) {
	const struct {
		float diagnostic;
		UnexpectedCode temperature;
		UnexpectedCode pressure;
	} cases[] = {
			{DiagnosticPressureFault, UnexpectedCode::Unknown, UnexpectedCode::Inconsistent},
			{DiagnosticTemperatureFault, UnexpectedCode::Inconsistent, UnexpectedCode::Unknown},
			{DiagnosticHardwareFault, UnexpectedCode::Inconsistent, UnexpectedCode::Inconsistent},
	};

	for (const auto& test : cases) {
		sendFrame(test.diagnostic, 20, 2.0f);
		sendFrame(test.diagnostic, 20, 2.0f);

		auto temperature = Sensor::get(SensorType::OilTemperature);
		auto pressure = Sensor::get(SensorType::OilPressure);
		if (test.temperature == UnexpectedCode::Unknown) {
			EXPECT_TRUE(temperature.Valid);
		} else {
			EXPECT_EQ(temperature.Code, test.temperature);
		}
		if (test.pressure == UnexpectedCode::Unknown) {
			EXPECT_TRUE(pressure.Valid);
		} else {
			EXPECT_EQ(pressure.Code, test.pressure);
		}
	}
}

TEST_F(HellaOpsT, RejectsInvalidPulseAndResynchronizes) {
	sendSymbol(1024, DiagnosticOk);
	sendSymbol(4096, 50);
	sendSymbol(4096, 704);

	auto temperature = Sensor::get(SensorType::OilTemperature);
	ASSERT_FALSE(temperature.Valid);
	EXPECT_EQ(temperature.Code, UnexpectedCode::Low);

	sendFrame(DiagnosticOk, 30, 2.0f);
	sendFrame(DiagnosticOk, 30, 2.0f);
	EXPECT_NEAR(Sensor::get(SensorType::OilTemperature).Value, 30, 0.5f);
	EXPECT_NEAR(Sensor::get(SensorType::OilPressure).Value, 100, 1);
}

TEST_F(HellaOpsT, RejectsDataWithoutSync) {
	sendSymbol(4096, 1280);
	sendSymbol(4096, 704);
	sendSymbol(4096, 1280);
	sendSymbol(4096, 704);

	EXPECT_FALSE(Sensor::get(SensorType::OilTemperature).Valid);
	EXPECT_FALSE(Sensor::get(SensorType::OilPressure).Valid);
}

TEST_F(HellaOpsT, UsesFallbackBaroAndClampsGaugePressure) {
	Sensor::setInvalidMockValue(SensorType::BarometricPressure);
	sendFrame(DiagnosticOk, 20, 1.0f);
	sendFrame(DiagnosticOk, 20, 1.0f);
	EXPECT_FLOAT_EQ(Sensor::get(SensorType::OilPressure).Value, 0);
}

TEST_F(HellaOpsT, TimesOutAfterAValidFrame) {
	sendFrame(DiagnosticOk, 20, 2.0f);
	sendFrame(DiagnosticOk, 20, 2.0f);
	ASSERT_TRUE(Sensor::get(SensorType::OilTemperature).Valid);
	ASSERT_TRUE(Sensor::get(SensorType::OilPressure).Valid);

	eth.moveTimeForwardMs(501);
	EXPECT_EQ(Sensor::get(SensorType::OilTemperature).Code, UnexpectedCode::Timeout);
	EXPECT_EQ(Sensor::get(SensorType::OilPressure).Code, UnexpectedCode::Timeout);

	sendFrame(DiagnosticOk, 20, 2.0f);
	sendFrame(DiagnosticOk, 20, 2.0f);
	EXPECT_TRUE(Sensor::get(SensorType::OilTemperature).Valid);
	EXPECT_TRUE(Sensor::get(SensorType::OilPressure).Valid);
}

TEST(HellaOpsTInit, PreservesExistingProviderForEachChannel) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	HellaOpsTSensor dut;

	StoredValueSensor existingPressure(SensorType::OilPressure, MS2NT(1000));
	ASSERT_TRUE(existingPressure.Register());
	dut.init(Gpio::A0);
	EXPECT_EQ(Sensor::getSensorOfType(SensorType::OilPressure), &existingPressure);
	EXPECT_NE(nullptr, Sensor::getSensorOfType(SensorType::OilTemperature));
	dut.deInit();
	EXPECT_EQ(Sensor::getSensorOfType(SensorType::OilPressure), &existingPressure);
	existingPressure.unregister();

	StoredValueSensor existingTemperature(SensorType::OilTemperature, MS2NT(1000));
	ASSERT_TRUE(existingTemperature.Register());
	dut.init(Gpio::A0);
	EXPECT_NE(nullptr, Sensor::getSensorOfType(SensorType::OilPressure));
	EXPECT_EQ(Sensor::getSensorOfType(SensorType::OilTemperature), &existingTemperature);
	dut.deInit();
	EXPECT_EQ(Sensor::getSensorOfType(SensorType::OilTemperature), &existingTemperature);
	existingTemperature.unregister();
}

TEST(HellaOpsTInit, ExistingProvidersSkipRegistrationAndReinitIsSafe) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	HellaOpsTSensor dut;

	StoredValueSensor existingPressure(SensorType::OilPressure, MS2NT(1000));
	StoredValueSensor existingTemperature(SensorType::OilTemperature, MS2NT(1000));
	ASSERT_TRUE(existingPressure.Register());
	ASSERT_TRUE(existingTemperature.Register());
	dut.init(Gpio::A0);
	EXPECT_EQ(Sensor::getSensorOfType(SensorType::OilPressure), &existingPressure);
	EXPECT_EQ(Sensor::getSensorOfType(SensorType::OilTemperature), &existingTemperature);
	dut.deInit();
	EXPECT_EQ(Sensor::getSensorOfType(SensorType::OilPressure), &existingPressure);
	EXPECT_EQ(Sensor::getSensorOfType(SensorType::OilTemperature), &existingTemperature);

	existingPressure.unregister();
	existingTemperature.unregister();
	dut.init(Gpio::A0);
	EXPECT_NE(nullptr, Sensor::getSensorOfType(SensorType::OilPressure));
	EXPECT_NE(nullptr, Sensor::getSensorOfType(SensorType::OilTemperature));
	dut.deInit();
	EXPECT_EQ(nullptr, Sensor::getSensorOfType(SensorType::OilPressure));
	EXPECT_EQ(nullptr, Sensor::getSensorOfType(SensorType::OilTemperature));
}
