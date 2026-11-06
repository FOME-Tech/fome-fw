#include "pch.h"

#include "can_imu.h"

static StoredValueSensor accelLat(SensorType::AccelLat, MS2NT(250));
static StoredValueSensor accelLon(SensorType::AccelLon, MS2NT(250));
static StoredValueSensor accelVert(SensorType::AccelVert, MS2NT(250));
static StoredValueSensor yawRate(SensorType::YawRate, MS2NT(250));

/*
 * Every decoder here must output the ISO 8855 sign convention documented on SensorType.
 *
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

/* BMW E8x/E9x MK60e5 DSC frames containing IMU data */
#define E90_DSC_DYNAMICS_CANID 0x1A0		// lateral acceleration, yaw rate
#define E90_DSC_ACCEL_LON_CANID 0x1A3		// longitudinal accelerometer, M3 only
#define E90_DSC_ACCEL_LON_LORES_CANID 0x2B3 // longitudinal accelerometer, low resolution, M3 and non-M

/* MK60e5 quantizations (from MK60e5_CAN.dbc) */
#define E90_ACCEL_LAT_QUANT 0.02501f	 // m/s2 per LSB
#define E90_YAW_QUANT 0.05f				 // deg/s per LSB
#define E90_ACCEL_LON_QUANT 0.01f		 // m/s2 per LSB, 0x1A3
#define E90_ACCEL_LON_LORES_QUANT 0.025f // m/s2 per LSB, 0x2B3

// The signed 12 bit signals use the most negative value to mean invalid
#define E90_INVALID_12BIT_SIGNED -2048
// The unsigned (offset binary) 12 bit signals use all ones to mean invalid
#define E90_INVALID_12BIT_UNSIGNED 0xFFF

// Extract a 12-bit little-endian (Intel) value starting at the given bit offset.
static uint16_t get12Bit_intel(const CANRxFrame& frame, int startBit) {
	int byteIndex = startBit / 8;
	int bitShift = startBit % 8;
	return ((frame.data8[byteIndex] | (frame.data8[byteIndex + 1] << 8)) >> bitShift) & 0x0FFF;
}

static int16_t get12BitSigned_intel(const CANRxFrame& frame, int startBit) {
	uint16_t raw = get12Bit_intel(frame, startBit);

	// sign extend from 12 bits
	if (raw & 0x0800) {
		return (int16_t)(raw | 0xF000);
	}
	return (int16_t)raw;
}

// The MK60e5 already uses the ISO 8855 convention for all of these, so no sign flips are needed
// (confirmed from a log).

// 0x1A3 and 0x2B3 carry the same accelerometer reading, but 0x1A3 is finer and faster. It's only sent by the M3, so
// fall back to 0x2B3 if we haven't recently seen a good 0x1A3.
static Timer e90AccelLonTimer;

static void decodeE90Dynamics(const CANRxFrame& frame, efitick_t nowNt) {
	// AccelLat, start bit 28, 12-bit signed
	int16_t accLat = get12BitSigned_intel(frame, 28);
	if (accLat != E90_INVALID_12BIT_SIGNED) {
		accelLat.setValidValue(accLat * E90_ACCEL_LAT_QUANT, nowNt);
	}

	// YawRate, start bit 40, 12-bit signed
	int16_t yaw = get12BitSigned_intel(frame, 40);
	if (yaw != E90_INVALID_12BIT_SIGNED) {
		yawRate.setValidValue(yaw * E90_YAW_QUANT, nowNt);
	}

	// AccelLongWheel (start bit 16) is ignored: it's derived from the front wheel speeds rather
	// than the accelerometer, heavily filtered, and wrong under front wheel slip/lock.
}

static void decodeE90AccelLon(const CANRxFrame& frame, efitick_t nowNt) {
	// AccelLongRawStatus, bits 24-25: 0 = valid
	if ((frame.data8[3] & 0x03) != 0) {
		return;
	}

	// AccelLongRaw, start bit 0, 12-bit signed
	int16_t raw = get12BitSigned_intel(frame, 0);
	if (raw == E90_INVALID_12BIT_SIGNED) {
		return;
	}

	// The raw value has no zero offset correction, so subtract the module's learned offset.
	// AccelLongOffset, start bit 12, 12-bit signed. AccelLongOffsetStatus, bits 26-27:
	// 0 = valid, 1 = not learned yet (still usable), 2 = invalid
	int16_t offset = get12BitSigned_intel(frame, 12);
	uint8_t offsetStatus = (frame.data8[3] >> 2) & 0x03;
	if (offsetStatus > 1 || offset == E90_INVALID_12BIT_SIGNED) {
		offset = 0;
	}

	accelLon.setValidValue((raw - offset) * E90_ACCEL_LON_QUANT, nowNt);
	e90AccelLonTimer.reset(nowNt);
}

static void decodeE90AccelLonLowRes(const CANRxFrame& frame, efitick_t nowNt) {
	// Five missed 0x1A3 frames before falling back
	if (!e90AccelLonTimer.hasElapsedMs(100)) {
		return;
	}

	// AccelLongStatus, bits 36-37: 0 = valid
	if (((frame.data8[4] >> 4) & 0x03) != 0) {
		return;
	}

	// AccelLongRaw, start bit 12, and AccelLongOffset, start bit 24: 12-bit offset binary, 2048 = 0
	int raw = get12Bit_intel(frame, 12);
	int offset = get12Bit_intel(frame, 24);

	if (raw == E90_INVALID_12BIT_UNSIGNED) {
		return;
	}

	if (offset == E90_INVALID_12BIT_UNSIGNED) {
		offset = 2048;
	}

	// Both share the same binary offset, so it cancels when subtracting
	accelLon.setValidValue((raw - offset) * E90_ACCEL_LON_LORES_QUANT, nowNt);
}

static void tryDecodeCanImuE90(const CANRxFrame& frame) {
	auto nowNt = getTimeNowNt();

	switch (CAN_SID(frame)) {
		case E90_DSC_DYNAMICS_CANID:
			decodeE90Dynamics(frame, nowNt);
			break;
		case E90_DSC_ACCEL_LON_CANID:
			decodeE90AccelLon(frame, nowNt);
			break;
		case E90_DSC_ACCEL_LON_LORES_CANID:
			decodeE90AccelLonLowRes(frame, nowNt);
			break;
	}
}

/* Continental/AUMOVIO MK 100 UHP ABS module IMU frames */
#define UHP_07_CANID 0x070 // lateral & longitudinal acceleration, yaw rate
#define UHP_08_CANID 0x080 // vertical acceleration

/* MK 100 UHP quantizations (from UHP_Vehicle_CAN.dbc) */
#define UHP_ACCEL_QUANT 0.025f	// m/s2 per LSB
#define UHP_ACCEL_OFFSET -51.2f // m/s2 at a raw value of 0
#define UHP_YAW_QUANT 0.01f		// deg/s per LSB

// Accelerations are 12 bit unsigned with an offset, reserving 0xFFE (initializing) and 0xFFF (failure).
static expected<float> uhpAccel(const CANRxFrame& frame, int startBit) {
	uint16_t raw = get12Bit_intel(frame, startBit);

	if (raw >= 0xFFE) {
		return unexpected;
	}

	return raw * UHP_ACCEL_QUANT + UHP_ACCEL_OFFSET;
}

static void tryDecodeCanImuMk100Uhp(const CANRxFrame& frame) {
	auto nowNt = getTimeNowNt();

	switch (CAN_SID(frame)) {
		case UHP_07_CANID:
			// UHP_LatAcc, bit 0, valid when UHP_LatAcc_Qf (bit 15) is clear
			if (!(frame.data8[1] & 0x80)) {
				if (auto lat = uhpAccel(frame, 0)) {
					accelLat.setValidValue(lat.Value, nowNt);
				}
			}

			// UHP_LongAcc, bit 16, valid when UHP_LongAcc_Qf (bit 31) is clear
			if (!(frame.data8[3] & 0x80)) {
				if (auto lon = uhpAccel(frame, 16)) {
					accelLon.setValidValue(lon.Value, nowNt);
				}
			}

			// UHP_YawRate, bits 32-45, valid when UHP_YawRate_Qf (bit 47) is clear.  It's an
			// unsigned magnitude, with UHP_YawRate_sgn (bit 46) set meaning negative.
			if (!(frame.data8[5] & 0x80)) {
				uint16_t raw = (frame.data8[4] | (frame.data8[5] << 8)) & 0x3FFF;

				// 0x3FFE (initializing) and 0x3FFF (failure) aren't yaw rates
				if (raw < 0x3FFE) {
					float yaw = raw * UHP_YAW_QUANT;
					yawRate.setValidValue((frame.data8[5] & 0x40) ? -yaw : yaw, nowNt);
				}
			}

			break;
		case UHP_08_CANID:
			// UHP_VertAcc, bit 0, valid when UHP_VertAcc_Qf (bit 15) is clear
			if (!(frame.data8[1] & 0x80)) {
				if (auto vert = uhpAccel(frame, 0)) {
					accelVert.setValidValue(vert.Value, nowNt);
				}
			}

			break;
	}
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
			switch (engineConfiguration->canVssNbcType) {
				case BMW_e90:
					tryDecodeCanImuE90(frame);
					break;
				case MK100_UHP:
					tryDecodeCanImuMk100Uhp(frame);
					break;
				default:
					break;
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
