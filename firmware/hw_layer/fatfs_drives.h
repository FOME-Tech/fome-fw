/**
 * @file	fatfs_drives.h
 * @brief	FatFS disk IO binding: routes each FatFS drive to a ChibiOS block device.
 */

#pragma once

#include "hal.h"

#ifdef __cplusplus
extern "C" {
#endif

// FatFS drive numbers. Paths with no drive prefix refer to drive 0, others look like "1:/file.txt"
#define FATFS_DRIVE_SD 0
// Read-only image containing the TunerStudio ini, the same one USB mass storage presents
#define FATFS_DRIVE_INI 1

// Set the block device behind a FatFS drive, or nullptr to detach it. The device must already be
// started: the binding never connects or disconnects a device, it only checks that it's ready.
void fatfsAttachDrive(uint8_t drive, BaseBlockDevice* device);

#ifdef __cplusplus
}
#endif
