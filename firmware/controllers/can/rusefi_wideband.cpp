#include "pch.h"

#if EFI_WIDEBAND_FIRMWARE_UPDATE && EFI_CAN_SUPPORT

#include "ch.h"
#include "can_msg_tx.h"
#include "rusefi_wideband.h"
#include "wideband_firmware/for_rusefi/wideband_can.h"

// This file contains an array called build_wideband_noboot_bin
// This array contains the firmware image for the wideband contoller
#include "wideband_firmware/for_rusefi/wideband_image.h"

#define EVT_BOOTLOADER_ACK EVENT_MASK(0)

static thread_t* waitingBootloaderThread = nullptr;

static inline void sendWidebandOnBothBuses(uint32_t id, uint8_t data[8] = {0}, size_t size = 0) {
	CanBusIndex bus[2] = {CanBusIndex::Bus1, CanBusIndex::Bus0};
	for(auto b : bus) {
		CanTxMessage m(id, 0, b, true);
		memcpy(&m[0], data, size);
	}
}

void handleWidebandBootloaderAck() {
	auto t = waitingBootloaderThread;
	if (t) {
		chEvtSignal(t, EVT_BOOTLOADER_ACK);
	}
}

bool waitAck() {
	return chEvtWaitAnyTimeout(EVT_BOOTLOADER_ACK, TIME_MS2I(1000)) != 0;
}

void updateWidebandFirmware() {

	// Clear any pending acks for this thread
	chEvtGetAndClearEvents(EVT_BOOTLOADER_ACK);

	// Send messages to the current thread when acks come in
	waitingBootloaderThread = chThdGetSelfX();

	efiPrintf("***************************************");
	efiPrintf("        WIDEBAND FIRMWARE UPDATE");
	efiPrintf("***************************************");
	efiPrintf("Wideband Update: Rebooting to bootloader...");

	// The first request will reboot the chip (if necessary), and the second one will enable bootloader mode
	// If the chip was already in bootloader (aka manual mode), then that's ok - the second request will
	// just be safely ignored (but acked)
	for (int i = 0; i < 2; i++) {
		{
			// Send bootloader entry command
			sendWidebandOnBothBuses(WB_BL_ENTER);
		}

		if (!waitAck()) {
			efiPrintf("Wideband Update ERROR: Expected ACK from entry to bootloader, didn't get one.");
			return;
		}

		// Let the controller reboot (and show blinky lights for a second before the update begins)
		chThdSleepMilliseconds(200);
	}

	efiPrintf("Wideband Update: in update mode, erasing flash...");

	{
		// Erase flash - opcode 1, magic value 0x5A5A
		sendWidebandOnBothBuses(0xEF1'5A5A);
		
	}

	if (!waitAck()) {
		efiPrintf("Wideband Update ERROR: Expected ACK from flash erase command, didn't get one.");
		return;
	}

	size_t totalSize = sizeof(build_wideband_image_bin);

	efiPrintf("Wideband Update: Flash erased! Sending %d bytes...", totalSize);

	// Send flash data 8 bytes at a time
	for (size_t i = 0; i < totalSize; i += 8) {
		{
			sendWidebandOnBothBuses(0xEF2'0000 + i, const_cast<uint8_t *>(build_wideband_image_bin) + i, 8);
		}

		if (!waitAck()) {
			efiPrintf("Wideband Update ERROR: Expected ACK from data write, didn't get one.");
			return;
		}
	}

	efiPrintf("Wideband Update: Update complete! Rebooting controller.");

	{
		// Reboot to firmware!
		sendWidebandOnBothBuses(0xEF3'0000);
	}

	waitAck();

	waitingBootloaderThread = nullptr;
}

void setWidebandOffset(uint8_t index) {
	// Clear any pending acks for this thread
	chEvtGetAndClearEvents(EVT_BOOTLOADER_ACK);

	// Send messages to the current thread when acks come in
	waitingBootloaderThread = chThdGetSelfX();

	efiPrintf("***************************************");
	efiPrintf("          WIDEBAND INDEX SET");
	efiPrintf("***************************************");
	efiPrintf("Setting all connected widebands to index %d...", index);

	{
		sendWidebandOnBothBuses(WB_MSG_SET_INDEX, &index, 1);
	}

	if (!waitAck()) {
		firmwareError(ObdCode::OBD_PCM_Processor_Fault, "Wideband index set failed: no controller detected!");
	}

	waitingBootloaderThread = nullptr;
}

void sendWidebandInfo() {

	float vbatt = Sensor::getOrZero(SensorType::BatteryVoltage) * 10;
	uint8_t data[2] = {static_cast<uint8_t>(vbatt), static_cast<uint8_t>(enginePins.o2heater.getLogicValue() ? 0x01 : 0x00)};
	sendWidebandOnBothBuses(WB_MGS_ECU_STATUS, data, 2);
}

#endif // EFI_WIDEBAND_FIRMWARE_UPDATE && HAL_USE_CAN
