/**
 * @file board.cpp
 *
 * @date Nov 15, 2013
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"

#include "boards.h"
#include "engine_sniffer.h"
#include "adc_math.h"
#include "backup_ram.h"

int getAdcValue(const char *msg, int hwChannel) {
	return 0;
}

// voltage in MCU universe, from zero to VDD
float getVoltage(const char *msg, adc_channel_e hwChannel) {
	return adcToVolts(getAdcValue(msg, hwChannel));
}

// Board voltage, with divider coefficient accounted for
float getVoltageDivided(const char *msg, adc_channel_e hwChannel) {
	return getVoltage(msg, hwChannel) * engineConfiguration->analogInputDividerCoefficient;
}

BackupSramData* getBackupSram() {
	static BackupSramData data;
	return &data;
}
