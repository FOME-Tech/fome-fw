/**
 * @file	can_hw.h
 *
 * @date Dec 11, 2013
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#pragma once

#include "efifeatures.h"

struct engine_configuration_s;

// Configuration sequence step, see config_steps.cpp. The peripherals and threads are only started at
// startup, so a change needs a reboot.
void startCan(const engine_configuration_s* prev);
bool canNeedsReboot();

void setCanType(int type);
void setCanVss(int type);

#if EFI_CAN_SUPPORT
const CANConfig* findCanConfig(can_baudrate_e rate);

bool getIsCanEnabled();
#endif /* EFI_CAN_SUPPORT */
