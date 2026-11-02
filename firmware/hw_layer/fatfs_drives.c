/**
 * @file	fatfs_drives.c
 * @brief	FatFS disk IO binding: routes each FatFS drive to a ChibiOS block device.
 *
 * FatFS calls the disk_xxx functions below with a drive number. Rather than hard-wiring one SD
 * card driver at compile time, each drive is whatever block device was attached to it, so the SD
 * card (SPI or SDMMC) and the ini ramdisk all go through the same few lines.
 */

#include "fatfs_drives.h"

#include "ff.h"
#include "diskio.h"

static BaseBlockDevice* drives[FF_VOLUMES];

void fatfsAttachDrive(uint8_t drive, BaseBlockDevice* device) {
	if (drive < FF_VOLUMES) {
		drives[drive] = device;
	}
}

static BaseBlockDevice* getReadyDrive(BYTE pdrv) {
	if (pdrv >= FF_VOLUMES) {
		return NULL;
	}

	BaseBlockDevice* device = drives[pdrv];

	if (!device || blkGetDriverState(device) != BLK_READY) {
		return NULL;
	}

	return device;
}

DSTATUS disk_status(BYTE pdrv) {
	BaseBlockDevice* device = getReadyDrive(pdrv);

	if (!device) {
		return STA_NOINIT;
	}

	return blkIsWriteProtected(device) ? STA_PROTECT : 0;
}

DSTATUS disk_initialize(BYTE pdrv) {
	// Devices are started by whoever attached them, so there's nothing to do but report status
	return disk_status(pdrv);
}

DRESULT disk_read(BYTE pdrv, BYTE* buff, DWORD sector, UINT count) {
	BaseBlockDevice* device = getReadyDrive(pdrv);

	if (!device) {
		return RES_NOTRDY;
	}

	return blkRead(device, sector, buff, count) == HAL_SUCCESS ? RES_OK : RES_ERROR;
}

#if !FF_FS_READONLY
DRESULT disk_write(BYTE pdrv, const BYTE* buff, DWORD sector, UINT count) {
	BaseBlockDevice* device = getReadyDrive(pdrv);

	if (!device) {
		return RES_NOTRDY;
	}

	if (blkIsWriteProtected(device)) {
		return RES_WRPRT;
	}

	return blkWrite(device, sector, buff, count) == HAL_SUCCESS ? RES_OK : RES_ERROR;
}
#endif // !FF_FS_READONLY

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
	BaseBlockDevice* device = getReadyDrive(pdrv);

	if (!device) {
		return RES_NOTRDY;
	}

	switch (cmd) {
		case CTRL_SYNC:
			// Writes are synchronous: by the time blkWrite returns, the data is on the card
			return RES_OK;
		case GET_SECTOR_COUNT: {
			BlockDeviceInfo info;
			if (blkGetInfo(device, &info) != HAL_SUCCESS) {
				return RES_ERROR;
			}
			*((DWORD*)buff) = info.blk_num;
			return RES_OK;
		}
		default:
			return RES_PARERR;
	}
}

DWORD get_fattime(void) {
#if HAL_USE_RTC
	RTCDateTime timespec;

	rtcGetTime(&RTCD1, &timespec);
	return rtcConvertDateTimeToFAT(&timespec);
#else
	return ((uint32_t)0 | (1 << 16)) | (1 << 21); /* wrong but valid time */
#endif
}
