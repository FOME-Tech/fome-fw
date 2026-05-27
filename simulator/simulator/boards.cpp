/**
 * @file board.cpp
 *
 * @date Nov 15, 2013
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"

#include "boards.h"
#include "engine_sniffer.h"
#include "backup_ram.h"

BackupSramData* getBackupSram() {
	static BackupSramData data;
	return &data;
}

float getAnalogInputDividerCoefficient(adc_channel_e) {
	return 1;
}
