#include "pch.h"

#include "can.h"

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

	efiPrintf("CAN_rx MM5_10_YAW_Y %f %f", yaw, accY);
	engine->sensors.accelerometer.yawRate = yaw * MM5_10_RATE_QUANT;
	engine->sensors.accelerometer.lat = accY * MM5_10_ACC_QUANT;
}

static void processCanRxImu_BoschM5_10_RollX(const CANRxFrame& frame) {
	float accX = getShiftedLSB_intel(frame, 4);
	efiPrintf("CAN_rx MM5_10_ROLL_X %f", accX);

	engine->sensors.accelerometer.lon = accX * MM5_10_ACC_QUANT;
}

static void processCanRxImu_BoschM5_10_Z(const CANRxFrame& frame) {
	float accZ = getShiftedLSB_intel(frame, 4);
	efiPrintf("CAN_rx MM5_10_Z %f", accZ);
	engine->sensors.accelerometer.vert = accZ * MM5_10_ACC_QUANT;
}

void processCanRxImu(const CANRxFrame& frame) {
	/*
		if (CAN_SID(frame) == 0x130) {
			float a = getShiftedLSB_intel(frame, 0);
			float b = getShiftedLSB_intel(frame, 4);
			efiPrintf("CAN_rx 130 %f %f", a, b);
		}

		if (engineConfiguration->imuType == IMU_VAG) {
			if (CAN_SID(frame) == VAG_YAW_RATE_G_LAT) {
				efiPrintf("CAN_rx VAG_YAW_RATE_G_LAT");
			} else if (CAN_SID(frame) == VAG_YAW_ACCEL_G_LONG) {
				efiPrintf("CAN_rx VAG_YAW_ACCEL_G_LONG");
			}
		}
		*/

	if (engineConfiguration->imuType == IMU_MM5_10) {
		if (CAN_SID(frame) == MM5_10_YAW_Y) {
			processCanRxImu_BoschM5_10_YawY(frame);
		} else if (CAN_SID(frame) == MM5_10_ROLL_X) {
			processCanRxImu_BoschM5_10_RollX(frame);
		} else if (CAN_SID(frame) == MM5_10_Z) {
			processCanRxImu_BoschM5_10_Z(frame);
		}
	} else if (engineConfiguration->imuType == IMU_TYPE_MB_A0065422618) {
		if (CAN_SID(frame) == MM5_10_MB_YAW_Y_CANID) {
			processCanRxImu_BoschM5_10_YawY(frame);
		} else if (CAN_SID(frame) == MM5_10_MB_ROLL_X_CANID) {
			processCanRxImu_BoschM5_10_RollX(frame);
		}
	}
}
