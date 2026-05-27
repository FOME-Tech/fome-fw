#pragma once

#include "can.h"

enum class WidebandUpdateState : uint8_t {
	None = 0,
	EnteringBootloader = 1,
	ErasingFlash = 2,
	SendingData = 3,
	Complete = 4,
	ErrorTargetedEntry = 5,
	ErrorBootloaderActivation = 6,
	ErrorFlashErase = 7,
	ErrorDataWrite = 8,
};

// Indicate that an ack response was received from the wideband bootloader
void handleWidebandBootloaderAck();
// Update the firmware on the wideband controller for the given lambda sensor index
void updateWidebandFirmware(uint8_t sensorIndex);
// Set the CAN index offset of any attached wideband controller
void setWidebandOffset(uint8_t index);
// Send info to the wideband controller like battery voltage, heater enable bit, etc.
void sendWidebandInfo(CanBusIndex bus);
