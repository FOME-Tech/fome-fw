#pragma once

#include "can.h"

// Indicate that an ack response was received from the wideband bootloader
void handleWidebandBootloaderAck();
// Update the firmware on the wideband controller for the given lambda sensor index
void updateWidebandFirmware(uint8_t sensorIndex);
// Set the CAN index offset of any attached wideband controller
void setWidebandOffset(uint8_t index);
// Send info to the wideband controller like battery voltage, heater enable bit, etc.
void sendWidebandInfo(CanBusIndex bus);
