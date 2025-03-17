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

// Initialize the SD card and mount its filesystem
// Returns true if the filesystem was successfully mounted for writing.
bool mountSdFilesystem();
void unmountSdFilesystem();

bool isSdCardAlive();

void onUsbConnectedNotifyMmcI();
