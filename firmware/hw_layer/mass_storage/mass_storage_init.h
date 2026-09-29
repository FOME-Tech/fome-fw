#pragma once

#include "hal.h"

#if HAL_USE_USB_MSD
void initUsbMsd();
#if EFI_FILE_LOGGING
void attachMsdSdCard(BaseBlockDevice* blkdev);
#endif
#endif
