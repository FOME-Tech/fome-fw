/*
 * @file obd2.cpp
 *
 * ISO 15765-4
 * http://en.wikipedia.org/wiki/OBD-II_PIDs
 *
 * @date Jun 9, 2015
 * @author Andrey Belomutskiy, (c) 2012-2020
 *
 * This file is part of rusEfi - see http://rusefi.com
 *
 * rusEfi is free software; you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * rusEfi is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 * even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include "pch.h"

#if EFI_CAN_SUPPORT || EFI_UNIT_TEST

#include "obd2.h"
#include "can.h"
#include "can_msg_tx.h"
#include "fuel_math.h"
#include "malfunction_central.h"

static const int16_t supportedPids0120[] = { 
	PID_MONITOR_STATUS,
	PID_FUEL_SYSTEM_STATUS,
	PID_ENGINE_LOAD,
	PID_COOLANT_TEMP,
	PID_STFT_BANK1,
	PID_STFT_BANK2,
	PID_INTAKE_MAP,
	PID_RPM,
	PID_SPEED,
	PID_TIMING_ADVANCE,
	PID_INTAKE_TEMP,
	PID_THROTTLE,
	-1
};

static const int16_t supportedPids2140[] = {
	PID_FUEL_AIR_RATIO_1,
	-1
};

static const int16_t supportedPids4160[] = { 
	PID_CONTROL_UNIT_VOLTAGE,
	PID_ETHANOL,
	PID_FUEL_RATE,
	PID_OIL_TEMPERATURE,
	-1
};

static void obdSendPacket(int mode, uint8_t pid, int numBytes, uint32_t iValue, CanBusIndex busIndex) {
	// Respond on the same bus we got the request from
	CanTxMessage resp(OBD_TEST_RESPONSE, 8, busIndex, false);

	// write number of bytes
	resp[0] = (uint8_t)(2 + numBytes);
	// write 2 bytes of header
	resp[1] = (uint8_t)(0x40 + mode);
	resp[2] = pid;
	// write N data bytes
	for (int i = 8 * (numBytes - 1), j = 3; i >= 0; i -= 8, j++) {
		resp[j] = (uint8_t)((iValue >> i) & 0xff);
	}
}

#define _1_MODE 1

static void obdSendValue(int mode, int PID, int numBytes, float value, CanBusIndex busIndex) {
	efiAssertVoid(ObdCode::CUSTOM_ERR_6662, numBytes <= 2, "invalid numBytes");
	int iValue = (int)efiRound(value, 1.0f);
	// clamp to uint8_t (0..255) or uint16_t (0..65535)
	iValue = maxI(minI(iValue, (numBytes == 1) ? 255 : 65535), 0);
	obdSendPacket(mode, PID, numBytes, iValue, busIndex);
}


//#define MOCK_SUPPORTED_PIDS 0xffffffff

static void obdWriteSupportedPids(uint8_t pid, int bitOffset, const int16_t *supportedPids, CanBusIndex busIndex) {
	uint32_t value = 0;
	// gather all 32 bit fields
	for (int i = 0; i < 32 && supportedPids[i] > 0; i++)
		value |= 1 << (31 + bitOffset - supportedPids[i]);

#ifdef MOCK_SUPPORTED_PIDS
	// for OBD debug
	value = MOCK_SUPPORTED_PIDS;
#endif

	obdSendPacket(1, pid, 4, value, busIndex);
}

static void obdStatusQuery(uint8_t pid, CanBusIndex busIndex) {
	static error_codes_set_s localErrorCopy;
	getErrorCodes(&localErrorCopy);

	CanTxMessage tx(OBD_TEST_RESPONSE, 8, busIndex, false);

	// set bit 7 if MIL on
	uint8_t byteA = (localErrorCopy.count > 0) ? (1 << 7) : 0x0;
	// bottom 7 bits contain code count
	byteA |= 0x7F & localErrorCopy.count;

	tx[0] = 0x6;
	tx[1] = 0x41;
	tx[2] = pid;
	tx[3] = byteA;

	// bytes B-D are all zeroes since we don't support readiness monitors
	tx[4] = 0x0;
	tx[5] = 0x0;
	tx[6] = 0x0;
}

static void handleGetDataRequest(uint8_t length, const CANRxFrame& rx, CanBusIndex busIndex) {
	if (length != 2) {
		// expect length 2: service + PID
		return;
	}
	
	auto pid = rx.data8[2];

	switch (pid) {
	case PID_SUPPORTED_PIDS_REQUEST_01_20:
		obdWriteSupportedPids(pid, 1, supportedPids0120, busIndex);
		break;
	case PID_SUPPORTED_PIDS_REQUEST_21_40:
		obdWriteSupportedPids(pid, 21, supportedPids2140, busIndex);
		break;
	case PID_SUPPORTED_PIDS_REQUEST_41_60:
		obdWriteSupportedPids(pid, 41, supportedPids4160, busIndex);
		break;
	case PID_MONITOR_STATUS:
		obdStatusQuery(pid, busIndex);
		break;
	case PID_FUEL_SYSTEM_STATUS:
		// todo: add statuses
		obdSendValue(_1_MODE, pid, 2, (2<<8)|(0), busIndex);	// 2 = "Closed loop, using oxygen sensor feedback to determine fuel mix"
		break;
	case PID_ENGINE_LOAD:
		obdSendValue(_1_MODE, pid, 1, getFuelingLoad() * ODB_TPS_BYTE_PERCENT, busIndex);
		break;
	case PID_COOLANT_TEMP:
		obdSendValue(_1_MODE, pid, 1, Sensor::getOrZero(SensorType::Clt) + ODB_TEMP_EXTRA, busIndex);
		break;
	case PID_STFT_BANK1:
		obdSendValue(_1_MODE, pid, 1, 128 * engine->stftCorrection[0], busIndex);
		break;
	case PID_STFT_BANK2:
		obdSendValue(_1_MODE, pid, 1, 128 * engine->stftCorrection[1], busIndex);
		break;
	case PID_INTAKE_MAP:
		obdSendValue(_1_MODE, pid, 1, Sensor::getOrZero(SensorType::Map), busIndex);
		break;
	case PID_RPM:
		obdSendValue(_1_MODE, pid, 2, Sensor::getOrZero(SensorType::Rpm) * ODB_RPM_MULT, busIndex);	//	rotation/min.	(A*256+B)/4
		break;
	case PID_SPEED:
		obdSendValue(_1_MODE, pid, 1, Sensor::getOrZero(SensorType::VehicleSpeed), busIndex);
		break;
	case PID_TIMING_ADVANCE: {
		float timing = engine->cylinders[0].getIgnitionTimingBtdc();
		timing = (timing > 360.0f) ? (timing - 720.0f) : timing;
		obdSendValue(_1_MODE, pid, 1, (timing + 64.0f) * 2.0f, busIndex);		// angle before TDC.	(A/2)-64
		break;
		}
	case PID_INTAKE_TEMP:
		obdSendValue(_1_MODE, pid, 1, Sensor::getOrZero(SensorType::Iat) + ODB_TEMP_EXTRA, busIndex);
		break;
	case PID_INTAKE_MAF:
		obdSendValue(_1_MODE, pid, 2, Sensor::getOrZero(SensorType::Maf) * 100.0f, busIndex);	// grams/sec	(A*256+B)/100
		break;
	case PID_THROTTLE:
		obdSendValue(_1_MODE, pid, 1, Sensor::getOrZero(SensorType::Tps1) * ODB_TPS_BYTE_PERCENT, busIndex);	// (A*100/255)
		break;
	case PID_FUEL_AIR_RATIO_1: {
		float lambda = clampF(0, Sensor::getOrZero(SensorType::Lambda1), 1.99f);

		uint16_t scaled = lambda * 32768;

		obdSendPacket(1, pid, 4, scaled << 16, busIndex);
		break;

	#ifdef MODULE_TRIP_ODO
	} case PID_FUEL_RATE: {
		float gPerSecond = engine->module<TripOdometer>()->getConsumptionGramPerSecond();
		float gPerHour = gPerSecond * 3600;
		float literPerHour = gPerHour * 0.00139f;
		obdSendValue(_1_MODE, pid, 2, literPerHour * 20.0f, busIndex);	//	L/h.	(A*256+B)/20
		break;
	#endif // MODULE_TRIP_ODO
	} case PID_CONTROL_UNIT_VOLTAGE: {
		obdSendValue(_1_MODE, pid, 2, 1000 * Sensor::getOrZero(SensorType::BatteryVoltage), busIndex);
		break;
	} case PID_ETHANOL: {
		obdSendValue(_1_MODE, pid, 1, (255.0f / 100) * Sensor::getOrZero(SensorType::FuelEthanolPercent), busIndex);
		break;
	} case PID_OIL_TEMPERATURE: {
		obdSendValue(_1_MODE, pid, 1, Sensor::getOrZero(SensorType::OilTemperature) + ODB_TEMP_EXTRA, busIndex);
		break;
	} default:
		// ignore unhandled PIDs
		break;
	}
}

template<typename T>
static void writeDtc(T& msg, size_t offset, ObdCode code) {
	msg[offset + 0] = (static_cast<uint16_t>(code) >> 8) & 0xFF;
	msg[offset + 1] = (static_cast<uint16_t>(code) >> 0) & 0xFF;
}

uint8_t responseBytes[MAX_ERROR_CODES_COUNT * 2];

static void handleDtcRequest(uint8_t service, int numCodes, ObdCode* dtcCode, CanBusIndex busIndex) {
	if (numCodes == 0) {
		// No DTCs: Respond with no trouble codes
		CanTxMessage tx(OBD_TEST_RESPONSE, 8, busIndex, false);
		tx[0] = 0x2;				// 2 data bytes
		tx[1] = 0x40 + service;		// Service $03 response
		tx[2] = 0x0;				// No DTCs
	} else if (numCodes <= 2) {
		// Response will fit in a single frame
		CanTxMessage tx(OBD_TEST_RESPONSE, 8, busIndex, false);
		tx[0] = 1 + 1 + 2 * numCodes;	// 1 (service) + 1 (num bytes) + 2*N (codes) data bytes
		tx[1] = 0x40 + service;		// Service $03 response
		tx[2] = numCodes;			// N stored codes

		for (int i = 0; i < numCodes; i++) {
			int dest = 3 + 2 * i;
			writeDtc(tx, dest, dtcCode[i]);
		}
	} else {
		// Assemble all codes in to the buffer
		for (size_t i = 0; i < numCodes; i++) {
			writeDtc(responseBytes, 2 * i, dtcCode[i]);
		}

		{
			// ISO-TP first frame
			CanTxMessage tx(OBD_TEST_RESPONSE, 8, busIndex, false);
			// Header
			tx[0] = 0x10;					// First frame
			tx[1] =	1 + 1 + 2 * numCodes;	// Total bytes (service + num codes + codes)

			// Start data
			tx[2] = 0x40 + service;			// Service $03 response
			tx[3] = numCodes;				// N stored codes

			for (size_t i = 0; i < 4; i++) {
				tx[4 + i] = responseBytes[i];
			}
		}

		uint8_t sequence = 1;
		auto readPtr = responseBytes + 4;
		size_t bytesRemain = 2 * numCodes - 4;

		while (bytesRemain) {
			CanTxMessage tx(OBD_TEST_RESPONSE, 8, busIndex, false);
			tx[0] = 0x20 + sequence;

			size_t chunkSize = bytesRemain > 7 ? 7 : bytesRemain;

			for (size_t i = 0; i < chunkSize; i++) {
				tx[i + 1] = readPtr[i];
			}

			sequence++;
			bytesRemain = bytesRemain - chunkSize;
			readPtr += chunkSize;
		}
	}
}

// #if HAL_USE_CAN || EFI_UNIT_TEST
void obdOnCanPacketRx(const CANRxFrame& rx, CanBusIndex busIndex) {
	if (CAN_SID(rx) != OBD_TEST_REQUEST) {
		return;
	}

	auto length = rx.data8[0];
	auto service = rx.data8[1];

	switch (service) {
	case OBD_CURRENT_DATA:
		handleGetDataRequest(length, rx, busIndex);
		break;
	case OBD_STORED_DTC:
		static error_codes_set_s localErrorCopy;
		getErrorCodes(&localErrorCopy);

		handleDtcRequest(service, localErrorCopy.count, localErrorCopy.error_codes, busIndex);
		break;
	case OBD_PENDING_DTC:
	case OBD_PERMANENT_DTC:
		// We don't support pending or permanent DTCs.
		handleDtcRequest(service, 0, nullptr, busIndex);
		break;
	}
}
// #endif /* HAL_USE_CAN */

#endif /* EFI_CAN_SUPPORT || EFI_UNIT_TEST */
