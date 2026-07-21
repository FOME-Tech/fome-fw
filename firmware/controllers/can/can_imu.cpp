#include "pch.h"

#include "can_imu.h"

static StoredValueSensor accelLat(SensorType::AccelLat, MS2NT(250));
static StoredValueSensor accelLon(SensorType::AccelLon, MS2NT(250));
static StoredValueSensor accelVert(SensorType::AccelVert, MS2NT(250));
static StoredValueSensor yawRate(SensorType::YawRate, MS2NT(250));

/*
 * TODO:
 *  - convert to CanListener
 *  - move to hw_layer/sensors/yaw_rate_sensor.cpp | accelerometer.cpp ?
 */

#define VAG_YAW_RATE_G_LAT 0x130
#define VAG_YAW_ACCEL_G_LONG 0x131

/* Bosch Acceleration Sensor MM5.10 quantizations */
#define MM5_10_RATE_QUANT 0.005
#define MM5_10_ACC_QUANT 0.0001274

/* Bosch Acceleration Sensor MM5.10 CAN IDs */
#define MM5_10_YAW_Y 0x174
#define MM5_10_ROLL_X 0x178
#define MM5_10_Z 0x17C

/* Mercedes pn: A 006 542 26 18 CAN IDs */
#define MM5_10_MB_YAW_Y_CANID 0x150
#define MM5_10_MB_ROLL_X_CANID 0x151

static uint16_t getLSB_intel(const CANRxFrame& frame, int offset) {
	return (frame.data8[offset + 1] << 8) + frame.data8[offset];
}

static int16_t getShiftedLSB_intel(const CANRxFrame& frame, int offset) {
	return getLSB_intel(frame, offset) - 0x8000;
}

static void processCanRxImu_BoschM5_10_YawY(const CANRxFrame& frame) {
	float yaw = getShiftedLSB_intel(frame, 0);
	float accY = getShiftedLSB_intel(frame, 4);

	auto nowNt = getTimeNowNt();
	yawRate.setValidValue(yaw * MM5_10_RATE_QUANT, nowNt);
	accelLat.setValidValue(accY * MM5_10_ACC_QUANT, nowNt);
}

static void processCanRxImu_BoschM5_10_RollX(const CANRxFrame& frame) {
	float accX = getShiftedLSB_intel(frame, 4);

	accelLon.setValidValue(accX * MM5_10_ACC_QUANT, getTimeNowNt());
}

static void processCanRxImu_BoschM5_10_Z(const CANRxFrame& frame) {
	float accZ = getShiftedLSB_intel(frame, 4);
	accelVert.setValidValue(accZ * MM5_10_ACC_QUANT, getTimeNowNt());
}

/* BMW E90 MK60e5 DSC "SPEED" message containing IMU data */
#define E90_DSC_IMU_CANID 416

/* MK60e5 quantizations (from MK60e5_CAN.dbc) */
#define E90_ACCEL_QUANT 0.025 // m/s2 per LSB
#define E90_YAW_QUANT 0.05	  // deg/s per LSB

// Extract a 12-bit signed little-endian (Intel) value starting at the given bit offset.
static int16_t get12BitSigned_intel(const CANRxFrame& frame, int startBit) {
	int byteIndex = startBit / 8;
	int bitShift = startBit % 8;
	uint16_t raw = (frame.data8[byteIndex] | (frame.data8[byteIndex + 1] << 8)) >> bitShift;
	raw &= 0x0FFF;

	// sign extend from 12 bits
	if (raw & 0x0800) {
		return (int16_t)(raw | 0xF000);
	}
	return (int16_t)raw;
}

static void tryDecodeCanImuE90(const CANRxFrame& frame) {
	if (CAN_SID(frame) != E90_DSC_IMU_CANID) {
		return;
	}

	// ACLN_VEH_LN_DSC: lateral acceleration, start bit 16, 12-bit signed
	int16_t accLat = get12BitSigned_intel(frame, 16);
	// ACLN_VEH_ACRO_DSC: longitudinal acceleration, start bit 28, 12-bit signed
	int16_t accLon = get12BitSigned_intel(frame, 28);
	// ANGV_YAW_DSC: yaw rate, start bit 40, 12-bit signed
	int16_t yaw = get12BitSigned_intel(frame, 40);

	auto nowNt = getTimeNowNt();
	accelLat.setValidValue(accLat * E90_ACCEL_QUANT, nowNt);
	accelLon.setValidValue(accLon * E90_ACCEL_QUANT, nowNt);
	yawRate.setValidValue(yaw * E90_YAW_QUANT, nowNt);
}

void processCanRxImu(const CANRxFrame& frame) {
	switch (engineConfiguration->imuType) {
		case IMU_MM5_10:
			if (CAN_SID(frame) == MM5_10_YAW_Y) {
				processCanRxImu_BoschM5_10_YawY(frame);
			} else if (CAN_SID(frame) == MM5_10_ROLL_X) {
				processCanRxImu_BoschM5_10_RollX(frame);
			} else if (CAN_SID(frame) == MM5_10_Z) {
				processCanRxImu_BoschM5_10_Z(frame);
			}
			break;
		case IMU_TYPE_MB_A0065422618:
			if (CAN_SID(frame) == MM5_10_MB_YAW_Y_CANID) {
				processCanRxImu_BoschM5_10_YawY(frame);
			} else if (CAN_SID(frame) == MM5_10_MB_ROLL_X_CANID) {
				processCanRxImu_BoschM5_10_RollX(frame);
			}
			break;
		default:
			// if none configured, check if your ABS module might provide it
			if (engineConfiguration->canVssNbcType == BMW_e90) {
				tryDecodeCanImuE90(frame);
			}
	}
}

void initCanImu() {
	if (engineConfiguration->imuType != IMU_NONE || engineConfiguration->enableCanVss) {
		accelLat.Register();
		accelLon.Register();
		accelVert.Register();
		yawRate.Register();
	}
}
