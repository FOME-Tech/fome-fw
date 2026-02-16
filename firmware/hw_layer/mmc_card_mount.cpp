#include "pch.h"

#if EFI_FILE_LOGGING && EFI_PROD_CODE

#include "mmc_card.h"

#include "ff.h"
#include "mass_storage_init.h"
#include "usbconsole.h"

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
	Timer t;
	t.reset();

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
#if HAL_USE_USB_MSD
		// No SD card - connect USB bus now so serial and INI drive still work
		connectUsbBus();
#endif
		return false;
	}

	// Mount filesystem temporarily for WiFi update check
	bool mounted = f_mount(sd_mem::getFs(), "/", 1) == FR_OK;

	if (mounted) {
#if EFI_WIFI
		// Check for WiFi firmware update/dump trigger files on SD card
		tryUpdateWifiFirmwareFromSd();
		tryDumpWifiFirmwareToSd();
#endif
		// Unmount — we'll either hand the card to USB or re-mount for logging
		f_mount(nullptr, "/", 0);
	} else {
		efiPrintf("SD card failed to mount filesystem");
	}

#if EFI_WIFI
	signalWifiSdUpdateComplete();
#endif

#if HAL_USE_USB_MSD
	// Now connect the USB bus - the host will enumerate and see the real SD card
	connectUsbBus();

	// Wait for the USB enumeration, or a 5 second timeout, whichever occurs first
	msg_t usbResult = usbConnectedSemaphore.wait(TIME_MS2I(5000));

	bool hasUsb = usbResult == MSG_OK;

	if (hasUsb) {
		// Mount the real card to USB
		attachMsdSdCard(cardBlockDevice);

		// At this point we're done: don't try to write files ourselves
		return false;
	}

	// Reclaim the card back from USB so that it doesn't get double mounted
	attachMsdSdCard(nullptr);
#endif

	// We were able to connect the SD card, mount the filesystem
	if (f_mount(sd_mem::getFs(), "/", 1) == FR_OK) {
		efiPrintf("SD card mounted!");
		fs_ready = true;
		return true;
	} else {
		efiPrintf("SD card failed to mount filesystem");
		return false;
	}
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
