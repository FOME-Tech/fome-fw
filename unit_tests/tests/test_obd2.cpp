#include "pch.h"

#include "can.h"
#include "obd2.h"
#include "can_msg_tx.h"
#include "malfunction_central.h"

using ::testing::ElementsAre;
using ::testing::InSequence;
using ::testing::StrictMock;

struct MockCanTxHandler : public ICanTransmitMock {
	MOCK_METHOD(void, onTx, (uint32_t id, uint8_t dlc, uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3, uint8_t d4, uint8_t d5, uint8_t d6, uint8_t d7), (override));
};

class Obd2 : public ::testing::Test {
protected:
	StrictMock<MockCanTxHandler> handler;

	void SetUp() override {
		setCanTxMockHandler(&handler);

		clearWarnings();
	}

	void TearDown() override {
		setCanTxMockHandler(nullptr);

		clearWarnings();
	}
};

void reqPid(uint8_t pid) {
	CANRxFrame frame;
	frame.SID = 0x7DF;
	frame.DLC = 8;
	frame.IDE = CAN_IDE_STD;
	setArrayValues(frame.data8, 0);
	frame.data8[0] = 2;		// data bytes to follow
	frame.data8[1] = 0x01;	// service 01
	frame.data8[2] = pid;

	obdOnCanPacketRx(frame, CanBusIndex::Bus0);
}

TEST_F(Obd2, PidOneByte) {
	// Coolant temp is single byte, CLT = A - 40
	Sensor::setMockValue(SensorType::Clt, 75);

	EXPECT_CALL(handler, onTx(0x7E8, 8,
		3, 0x41, 0x05,	// len, service, pid
		75 + 40,		// temp
		0, 0, 0, 0		// unused
	));

	// PID 0x05 = coolant temperature
	reqPid(5);
}

TEST_F(Obd2, PidTwoBytes) {
	// Coolant temp is single byte, [A,B] = RPM * 4
	Sensor::setMockValue(SensorType::Rpm, 1000.25);

	EXPECT_CALL(handler, onTx(0x7E8, 8,
		4, 0x41, 0x0C,	// len, service, pid
		0x0F, 0xA1,		// RPM
		0, 0, 0			// unused
	));

	// PID 0xC = RPM
	reqPid(0x0C);
}

TEST_F(Obd2, MonitorStatusStatusNoDtc) {
	uint8_t expected = 0;

	EXPECT_CALL(handler, onTx(0x7E8, 8,
		6, 0x41, 0x01,	// len, service, pid
		expected,		// DTC status
		0, 0, 0, 0		// unused
	));

	// PID 0x01 = Monitor status
	reqPid(0x01);
}

TEST_F(Obd2, MonitorStatusStatusWithDtc) {
	// MIL on, 3 codes set
	uint8_t expected = (1 << 7) | 3;

	EXPECT_CALL(handler, onTx(0x7E8, 8,
		6, 0x41, 0x01,	// len, service, pid
		expected,		// DTC status
		0, 0, 0, 0		// unused
	));

	// Set 3 codes
	addError(ObdCode::OBD_TPS1_Primary_High);
	addError(ObdCode::OBD_Clt_Low);
	addError(ObdCode::OBD_FlexSensor_Timeout);

	// PID 0x01 = Monitor status
	reqPid(0x01);
}

static void requestDtcs() {
	CANRxFrame frame;
	frame.SID = 0x7DF;
	frame.DLC = 8;
	frame.IDE = CAN_IDE_STD;
	setArrayValues(frame.data8, 0);
	frame.data8[0] = 0x01;	// data bytes to follow
	frame.data8[1] = 0x03;	// service 03: stored codes

	obdOnCanPacketRx(frame, CanBusIndex::Bus0);
}

TEST_F(Obd2, ReadDtcsZero) {
	// No codes are set
	clearWarnings();

	EXPECT_CALL(handler, onTx(0x7E8, 8,
		2, 0x43, 0,		// len, service, DTC count (0)
		0, 0,			// First code
		0, 0,			// Second code
		0				// Padding
	));

	requestDtcs();
}

TEST_F(Obd2, ReadDtcsOne) {
	// Set a code, P0123
	addError(ObdCode::OBD_TPS1_Primary_High);

	EXPECT_CALL(handler, onTx(0x7E8, 8,
		4, 0x43, 1,		// len, service, DTC count (1)
		0x01, 0x23,		// First code: P0123
		0, 0,			// Second code: none
		0				// Padding
	));

	requestDtcs();
}

TEST_F(Obd2, ReadDtcsTwo) {
	// Set some codes
	addError(ObdCode::OBD_TPS1_Primary_High);
	addError(ObdCode::OBD_PCM_MainRelayFault);

	EXPECT_CALL(handler, onTx(0x7E8, 8,
		6, 0x43, 2,		// len, service, DTC count (1)
		0x01, 0x23,		// First code: P0123
		0x06, 0x12,		// Second code: P0612
		0				// Padding
	));

	requestDtcs();
}

TEST_F(Obd2, ReadDtcsThree) {
	// Set some codes
	addError(ObdCode::OBD_TPS1_Primary_High);
	addError(ObdCode::OBD_PCM_MainRelayFault);
	addError(ObdCode::OBD_FlexSensor_Timeout);

	{
		InSequence is;

		EXPECT_CALL(handler, onTx(0x7E8, 8,
			0x10, 0x08,		// First frame, total length 8
			0x43, 0x03,		// service code, DTC count
			0x01, 0x23,		// First code P0123
			0x06, 0x12		// Second code: P0612
		));

		EXPECT_CALL(handler, onTx(0x7E8, 8,
			0x21,			// Header, sequence number 1
			0x01, 0x76,		// Third code: P0176
			0, 0, 0, 0, 0	// padding
		));
	}

	requestDtcs();
}

TEST_F(Obd2, ReadDtcsEight) {
	// Set 8 codes
	addError(ObdCode::OBD_TPS1_Primary_High);
	addError(ObdCode::OBD_PCM_MainRelayFault);
	addError(ObdCode::OBD_FlexSensor_Timeout);
	addError(ObdCode::OBD_PCM_Processor_Fault);
	addError(ObdCode::Sensor5vSupplyLow);
	addError(ObdCode::Sensor5vSupplyHigh);
	addError(ObdCode::OBD_Throttle_Actuator_Control_Range_Performance_Bank_1);
	addError(ObdCode::OBD_PPS_Primary_Low);

	{
		InSequence is;

		EXPECT_CALL(handler, onTx(0x7E8, 8,
			0x10, 0x12,		// First frame, total length 18
			0x43, 0x08,		// service code, DTC count
			0x01, 0x23,		// First code P0123
			0x06, 0x12		// Second code: P0612
		));

		EXPECT_CALL(handler, onTx(0x7E8, 8,
			0x21,			// Consecutive frame, sequence number 1
			0x01, 0x76,		// Third code: P0176
			0x06, 0x06,		// Fourth: P0606
			0x06, 0x42,		// Fifth: P0642
			0x06			// First half of the 6th code: P0643
		));

		EXPECT_CALL(handler, onTx(0x7E8, 8,
			0x22,			// Consecutive frame, sequence number 2
			0x43,			// Second half of the 6th code: P0643
			0x06, 0x38,		// 7th code: P0638
			0x21, 0x27,		// 8th code: P2127
			0x00, 0x00		// padding
		));
	}

	requestDtcs();
}
