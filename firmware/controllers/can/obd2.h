/*
 * @file obd2.h
 *
 * @date Jun 9, 2015
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#pragma once

#include "can.h"

#define OBD_TEST_REQUEST 0x7DF

#define OBD_TEST_RESPONSE 0x7E8

#define OBD_CURRENT_DATA 0x01
#define OBD_STORED_DTC 0x03
#define OBD_PENDING_DTC 0x07
#define OBD_PERMANENT_DTC 0x0A

// https://en.wikipedia.org/wiki/OBD-II_PIDs

#define PID_SUPPORTED_PIDS_REQUEST_01_20 0x00
#define PID_MONITOR_STATUS 0x01
#define PID_FUEL_SYSTEM_STATUS 0x03
#define PID_ENGINE_LOAD 0x04
#define PID_COOLANT_TEMP 0x05
#define PID_STFT_BANK1 0x06
#define PID_STFT_BANK2 0x08
#define PID_FUEL_PRESSURE 0x0A
#define PID_INTAKE_MAP 0x0B
#define PID_RPM 0x0C
#define PID_SPEED 0x0D
#define PID_TIMING_ADVANCE 0x0E
#define PID_INTAKE_TEMP 0x0F
#define PID_INTAKE_MAF 0x10
#define PID_THROTTLE 0x11

#define PID_SUPPORTED_PIDS_REQUEST_21_40 0x20
#define PID_FUEL_AIR_RATIO_1 0x24

#define PID_SUPPORTED_PIDS_REQUEST_41_60 0x40
#define PID_CONTROL_UNIT_VOLTAGE 0x42
#define PID_ETHANOL 0x52
#define PID_OIL_TEMPERATURE 0x5C
#define PID_FUEL_RATE 0x5E

#if HAL_USE_CAN || EFI_UNIT_TEST
void obdOnCanPacketRx(const CANRxFrame& rx, CanBusIndex busIndex);
#endif /* HAL_USE_CAN */

#define ODB_RPM_MULT 4
#define ODB_TEMP_EXTRA 40
#define ODB_TPS_BYTE_PERCENT 2.55f
