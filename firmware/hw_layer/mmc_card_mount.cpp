#include "pch.h"

#if EFI_FILE_LOGGING && EFI_PROD_CODE

#include "mmc_card.h"

#include "ff.h"
#include "mass_storage_init.h"

#if EFI_WIFI
#include "wifi_sd_firmware_updater.h"
#endif

static bool fs_ready = false;

#if HAL_USE_USB_MSD
static chibios_rt::BinarySemaphore usbConnectedSemaphore(/* taken =*/true);

void onUsbConnectedNotifyMmcI() {
	usbConnectedSemaphore.signalI();
}
#endif /* HAL_USE_USB_MSD */

bool mountSdFilesystem() {
	auto cardBlockDevice = initializeMmcBlockDevice();

#if EFI_TUNER_STUDIO
	// If not null, card is present
	engine->outputChannels.sd_present = cardBlockDevice != nullptr;
#endif

	// if no card, don't try to mount FS
	if (!cardBlockDevice) {
#if EFI_WIFI
		signalWifiSdUpdateComplete();
#endif
		return false;
	}

	// Mount filesystem immediately (before USB wait)
	bool mounted = f_mount(sd_mem::getFs(), "/", 1) == FR_OK;

	if (mounted) {
#if EFI_WIFI
		// Check for WiFi firmware update/dump trigger files on SD card
		tryUpdateWifiFirmwareFromSd();
		tryDumpWifiFirmwareToSd();
#endif
	} else {
		efiPrintf("SD card failed to mount filesystem");
	}

#if EFI_WIFI
	signalWifiSdUpdateComplete();
#endif

#if HAL_USE_USB_MSD
	// Wait for the USB stack to wake up, or a 5 second timeout, whichever occurs first
	msg_t usbResult = usbConnectedSemaphore.wait(TIME_MS2I(5000));

	bool hasUsb = usbResult == MSG_OK;

	if (hasUsb) {
		// Unmount filesystem (if mounted) before handing card to USB
		if (mounted) {
			f_mount(nullptr, "/", 0);
		}
		attachMsdSdCard(cardBlockDevice);

		// At this point we're done: don't try to write files ourselves
		return false;
	}
#endif

	if (!mounted) {
		stopMmcBlockDevice();
		return false;
	}

	// No USB — filesystem already mounted, proceed with logging
	efiPrintf("SD card mounted!");
	fs_ready = true;
	return true;
}

void unmountSdFilesystem() {
	if (!fs_ready) {
		efiPrintf("Error: No File system is mounted. \"mountsd\" first");
		return;
	}

	fs_ready = false;

	// Unmount the volume
	f_mount(nullptr, nullptr, 0); // FatFs: Unregister work area prior to discard it

	stopMmcBlockDevice();

	efiPrintf("MMC/SD card removed");
}

#endif // EFI_FILE_LOGGING && EFI_PROD_CODE
