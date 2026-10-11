#pragma once

#include "hal.h"

#if HAL_USE_USB_MSD
void initUsbMsd();
void attachMsdSdCard(BaseBlockDevice* blkdev);

// The read-only FAT image containing the TunerStudio ini, or nullptr if this build doesn't embed one.
BaseBlockDevice* getIniBlockDevice();
#else
inline BaseBlockDevice* getIniBlockDevice() {
	return nullptr;
}
#endif
