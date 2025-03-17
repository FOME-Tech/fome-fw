/*
 * @file    mmc_card.h
 *
 *
 * @date Dec 30, 2013
 * @author Kot_dnz
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#pragma once

#include "tunerstudio_io.h"

#define DOT_MLG ".mlg"

void initSdCardLogger();
bool isSdCardAlive();

void onUsbConnectedNotifyMmcI();

struct USBDriver;
bool msd_request_hook_new(USBDriver *usbp);
