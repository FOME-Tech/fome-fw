#include "pch.h"

#include "AemXSeriesLambda.h"

#define CHECKBUS(d, bus, id, result)	do { frame.SID = id; EXPECT_EQ(result, d.acceptFrame(bus, frame)); } while (0)
#define CHECK(id, result)				CHECKBUS(dut, CanBusIndex::Bus0, id, result)

TEST(CanWideband, AcceptFrameId0) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	AemXSeriesWideband dut(0, SensorType::Lambda1);

	CANRxFrame frame;

	frame.IDE = false;
	frame.DLC = 8;

	// Check that the AEM format frame is accepted, but not FOME
	engineConfiguration->widebandMode = WidebandMode::AemXSeries;
	CHECK(0x180, true);
	CHECK(0x190, false);
	CHECK(0x191, false);

	// Check that the FOME format frame is accepted, but not AEM
	engineConfiguration->widebandMode = WidebandMode::FOMEInternal;
	CHECK(0x180, false);
	CHECK(0x190, true);
	CHECK(0x191, true);
}

TEST(CanWideband, AcceptFrameId1) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	AemXSeriesWideband dut(1, SensorType::Lambda2);

	CANRxFrame frame;

	frame.IDE = false;
	frame.DLC = 8;

	// Check that the AEM format frame is accepted, but not FOME
	engineConfiguration->widebandMode = WidebandMode::AemXSeries;
	CHECK(0x181, true);
	CHECK(0x192, false);
	CHECK(0x193, false);

	// Check that the FOME format frame is accepted, but not AEM
	engineConfiguration->widebandMode = WidebandMode::FOMEInternal;
	CHECK(0x181, false);
	CHECK(0x192, true);
	CHECK(0x193, true);
}

TEST(CanWideband, AcceptChecksBus) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	engineConfiguration->widebandMode = WidebandMode::AemXSeries;

	AemXSeriesWideband dut1(0, SensorType::Lambda1);
	AemXSeriesWideband dut2(1, SensorType::Lambda2);

	CANRxFrame frame;

	frame.IDE = false;
	frame.DLC = 8;

	// Channel 1: bus 0, index 3
	config->lambdaSensorSourceBus[0] = 0;
	config->lambdaSensorSourceIndex[0] = 3;
	auto bus0 = CanBusIndex::Bus0;

	// Channel 2: bus 1, index 2
	config->lambdaSensorSourceBus[1] = 1;
	config->lambdaSensorSourceIndex[1] = 2;
	auto bus1 = CanBusIndex::Bus1;

	// Check that Sensor1 only listens to bus 0, index 3
	CHECKBUS(dut1, CanBusIndex::Bus0, 0x182, false);
	CHECKBUS(dut1, CanBusIndex::Bus0, 0x183, true);
	CHECKBUS(dut1, CanBusIndex::Bus1, 0x182, false);
	CHECKBUS(dut1, CanBusIndex::Bus1, 0x183, false);

	// Check that Sensor2 only listens to bus 0, index 3
	CHECKBUS(dut2, CanBusIndex::Bus0, 0x182, false);
	CHECKBUS(dut2, CanBusIndex::Bus0, 0x183, false);
	CHECKBUS(dut2, CanBusIndex::Bus1, 0x182, true);
	CHECKBUS(dut2, CanBusIndex::Bus1, 0x183, false);
}

TEST(CanWideband, DecodeValidAemFormat) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->widebandMode = WidebandMode::AemXSeries;

	AemXSeriesWideband dut(0, SensorType::Lambda1);
	dut.Register();

	// check not set
	EXPECT_FLOAT_EQ(-1, Sensor::get(SensorType::Lambda1).value_or(-1));

	CANRxFrame frame;

	frame.SID = 0x180;
	frame.IDE = false;

	frame.DLC = 8;

	frame.data8[0] = 0x1F;	// 8000, lambda 0.8
	frame.data8[1] = 0x40;
	frame.data8[2] = 0;
	frame.data8[3] = 0;
	frame.data8[4] = 0;
	frame.data8[5] = 0;
	frame.data8[6] =
		1 << 1 |	// LSU 4.9 detected
		1 << 7;		// Data valid
	frame.data8[7] = 0;

	// check that lambda updates
	dut.processFrame(CanBusIndex::Bus0, frame, getTimeNowNt());
	EXPECT_FLOAT_EQ(0.8f, Sensor::get(SensorType::Lambda1).value_or(-1));


	// Now check invalid data
	frame.data8[6] =
		1 << 1 |	// LSU 4.9 detected
		0 << 7;		// Data INVALID

	dut.processFrame(CanBusIndex::Bus0, frame, getTimeNowNt());
	EXPECT_FLOAT_EQ(-1, Sensor::get(SensorType::Lambda1).value_or(-1));


	// Now check sensor fault
	frame.data8[6] =
		1 << 1 |	// LSU 4.9 detected
		1 << 7;		// Data valid
	frame.data8[7] = 1 << 6; // Sensor fault!

	dut.processFrame(CanBusIndex::Bus0, frame, getTimeNowNt());
	EXPECT_FLOAT_EQ(-1, Sensor::get(SensorType::Lambda1).value_or(-1));

	Sensor::resetRegistry();
}

#include "wideband_firmware/for_rusefi/wideband_can.h"

TEST(CanWideband, DecodeRusefiStandard) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->widebandMode = WidebandMode::FOMEInternal;

	AemXSeriesWideband dut(0, SensorType::Lambda1);
	dut.Register();

	CANRxFrame frame;
	frame.SID = 0x190;
	frame.IDE = false;
	frame.DLC = 8;

	// version
	frame.data8[0] = RUSEFI_WIDEBAND_VERSION;

	// valid
	frame.data8[1] = 1;

	// data = 0.7 lambda
	*reinterpret_cast<uint16_t*>(&frame.data8[2]) = 7000;

	// data = 1234 deg C
	*reinterpret_cast<uint16_t*>(&frame.data8[4]) = 1234;

	// check not set
	EXPECT_FLOAT_EQ(-1, Sensor::get(SensorType::Lambda1).value_or(-1));

	// check that lambda updates
	dut.processFrame(CanBusIndex::Bus0, frame, getTimeNowNt());
	EXPECT_FLOAT_EQ(0.7f, Sensor::get(SensorType::Lambda1).value_or(-1));

	// Check that temperature updates
	EXPECT_EQ(dut.tempC, 1234);

	// Check that valid bit is respected (should be invalid now)
	frame.data8[1] = 0;
	dut.processFrame(CanBusIndex::Bus0, frame, getTimeNowNt());
	EXPECT_FLOAT_EQ(-1, Sensor::get(SensorType::Lambda1).value_or(-1));
}

TEST(CanWideband, DecodeRusefiStandardWrongVersion) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->widebandMode = WidebandMode::FOMEInternal;

	AemXSeriesWideband dut(0, SensorType::Lambda1);
	dut.Register();

	CANRxFrame frame;
	frame.SID = 0x190;
	frame.IDE = false;
	frame.DLC = 8;

	// version - WRONG VERSION ON PURPOSE!
	frame.data8[0] = RUSEFI_WIDEBAND_VERSION + 1;

	EXPECT_FATAL_ERROR(dut.processFrame(CanBusIndex::Bus0, frame, getTimeNowNt()));
}
