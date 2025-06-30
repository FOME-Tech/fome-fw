/**
 * @file boards.cpp
 *
 * @date Nov 15, 2013
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"

#include "boards.h"
#include "backup_ram.h"

float getVoltageDivided(const char *msg, adc_channel_e hwChannel) {
	return 0;
}

float getVoltage(const char *msg, adc_channel_e hwChannel) {
	return 0;
}

int getAdcValue(const char *msg, adc_channel_e hwChannel) {
	return 0;
}

BackupSramData* getBackupSram() {
	static BackupSramData data;
	return &data;
}
