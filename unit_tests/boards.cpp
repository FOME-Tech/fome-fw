/**
 * @file boards.cpp
 *
 * @date Nov 15, 2013
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"

#include "boards.h"
#include "backup_ram.h"

BackupSramData* getBackupSram() {
	static BackupSramData data;
	return &data;
}
