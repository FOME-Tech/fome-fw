/**
 * @file	can_rx.cpp
 *
 * CAN reception handling.  This file handles multiplexing incoming CAN frames as appropriate
 * to the subsystems that consume them.
 *
 * @date Mar 19, 2020
 * @author Matthew Kennedy, (c) 2020
 */

#include "pch.h"

#include "rusefi_lua.h"
#include "can_bench_test.h"

void processCanRxImu(const CANRxFrame& frame);

typedef float SCRIPT_TABLE_8x8_f32t_linear[SCRIPT_TABLE_8 * SCRIPT_TABLE_8];

bool acceptCanRx(int /*sid*/) {
	if (!engineConfiguration->usescriptTableForCanSniffingFiltering) {
		// accept anything if filtering is not enabled
		return true;
	}
	/*
		// the whole table reuse and 2D table cast to 1D array is a major hack, but it's OK for prototyping
		SCRIPT_TABLE_8x8_f32t_linear *array =
				(SCRIPT_TABLE_8x8_f32t_linear*) (void*) &config->scriptTable1;

		int arraySize = efi::size(*array);

		int showOnlyCount = (int) array[arraySize - 1];
		if (showOnlyCount > 0 && showOnlyCount < arraySize) {
			for (int i = 0; i < showOnlyCount; i++) {
				if (sid == (int) array[arraySize - 2 - i]) {
					return true;
				}
			}
			// if white list is not empty and element not on the white list we do not check ignore list
			return false;
		}

		int ignoreListCount = (int) array[0];
		if (ignoreListCount > 0 && ignoreListCount < arraySize) {
			for (int i = 0; i < ignoreListCount; i++) {
				if (sid == (int) array[1 + i]) {
					// element is in ignore list
					return false;
				}
			}
		}
	*/
	return true;
}

#if EFI_CAN_SUPPORT

#include "can.h"
#include "obd2.h"
#include "can_sensor.h"
#include "can_vss.h"
#include "rusefi_wideband.h"

/**
 * this build-in CAN sniffer is very basic but that's our CAN sniffer
 */
static void printPacket(CanBusIndex busIndex, const CANRxFrame& rx) {
	//	bool accept = acceptCanRx(CAN_SID(rx));
	//	if (!accept) {
	//		return;
	//	}

	// only print info if we're in can debug mode

	int id = CAN_ID(rx);

	// internet people use both hex and decimal to discuss packed IDs, for usability it's better to print both right
	// here
	efiPrintf(
			"CAN RX bus %d ID %x(%d) DLC %d: %02x %02x %02x %02x %02x %02x %02x %02x",
			static_cast<size_t>(busIndex),
			id,
			id, // once in hex, once in dec
			rx.DLC,
			rx.data8[0],
			rx.data8[1],
			rx.data8[2],
			rx.data8[3],
			rx.data8[4],
			rx.data8[5],
			rx.data8[6],
			rx.data8[7]);
}
struct CanListenerTailSentinel : public CanListener {
	CanListenerTailSentinel()
		: CanListener(0) {}

	bool acceptFrame(CanBusIndex, const CANRxFrame&) const override {
		return false;
	}

	void decodeFrame(const CANRxFrame&, efitick_t) override {
		// nothing to do
	}
};

static CanListenerTailSentinel tailSentinel;
CanListener* canListeners_head = &tailSentinel;

static void serviceCanSubscribers(CanBusIndex busIndex, const CANRxFrame& frame, efitick_t nowNt) {
	CanListener* current = canListeners_head;

	while (current) {
		current = current->processFrame(busIndex, frame, nowNt);
	}
}

void registerCanListener(CanListener& listener) {
	// If the listener already has a next, it's already registered
	if (!listener.hasNext()) {
		listener.setNext(canListeners_head);
		canListeners_head = &listener;
	}
}

void registerCanSensor(CanSensorBase& sensor) {
	registerCanListener(sensor);
	sensor.Register();
}

static StoredValueSensor canEgts[8] = {
		{SensorType::EGT1, MS2NT(500)},
		{SensorType::EGT2, MS2NT(500)},
		{SensorType::EGT3, MS2NT(500)},
		{SensorType::EGT4, MS2NT(500)},
		{SensorType::EGT5, MS2NT(500)},
		{SensorType::EGT6, MS2NT(500)},
		{SensorType::EGT7, MS2NT(500)},
		{SensorType::EGT8, MS2NT(500)},
};

static bool didRegisterCanEgt = false;

static void processEgtCan(CanBusIndex busIndex, const CANRxFrame& frame) {
	if (!engineConfiguration->ecumasterEgtToCan) {
		return;
	}

	if (didRegisterCanEgt) {
		didRegisterCanEgt = true;

		for (auto& egt : canEgts) {
			egt.Register();
		}
	}

	size_t offset = 0;

	auto baseId = engineConfiguration->ecumasterEgtToCanBaseId;

	if (CAN_SID(frame) == baseId + 0) {
		offset = 0;
	} else if (CAN_SID(frame) == baseId + 1) {
		offset = 4;
	} else {
		return;
	}

	auto now = getTimeNowNt();

	for (int i = 0; i < 4; i++) {
		canEgts[i + offset].setValidValue(frame.data16[i], now);
	}
}

static void processCanInputPins(CanBusIndex busIndex, const CANRxFrame& frame) {
	for (size_t i = 0; i < CAN_VIRTUAL_INPUT_PINS_COUNT; i++) {
		const auto& inputConf = engineConfiguration->canVirtualInputs[i];

		if (CAN_ID(frame) != inputConf.id) {
			continue;
		}

		// extract the requested bit
		bool value = (frame.data8[inputConf.byte] >> inputConf.bitOffset) & 0x01;

		setCanVirtualInput(i, value);
	}
}

void processCanRxMessage(CanBusIndex busIndex, const CANRxFrame& frame, efitick_t nowNt) {
	if (engineConfiguration->verboseCan && busIndex == CanBusIndex::Bus0) {
		printPacket(busIndex, frame);
	} else if (engineConfiguration->verboseCan2 && busIndex == CanBusIndex::Bus1) {
		printPacket(busIndex, frame);
	}

	serviceCanSubscribers(busIndex, frame, nowNt);

	// todo: convert to CanListener or not?
	// Vss is configurable, should we handle it here:
	processCanRxVss(frame, nowNt);

	processCanRxImu(frame);

	processCanBenchTest(frame);

	processLuaCan(busIndex, frame);

	processEgtCan(busIndex, frame);

	processCanInputPins(busIndex, frame);

	obdOnCanPacketRx(frame, busIndex);

#if EFI_WIDEBAND_FIRMWARE_UPDATE
	// Bootloader acks with address 0x727573 aka ascii "rus"
	if (CAN_EID(frame) == 0x727573) {
		handleWidebandBootloaderAck();
	}
#endif
#if EFI_USE_OPENBLT
	if ((CAN_SID(frame) == 0x667) && (frame.DLC == 2)) {
		// TODO: gracefull shutdown?
		if (((busIndex == CanBusIndex::Bus0) && (engineConfiguration->canOpenBLT)) ||
			((busIndex == CanBusIndex::Bus1) && (engineConfiguration->can2OpenBLT))) {
			jump_to_openblt();
		}
	}
#endif
}

#endif // EFI_CAN_SUPPORT
