#include "pch.h"

#if EFI_FILE_LOGGING && EFI_PROD_CODE

#include "mmc_card.h"
#include "dma_buffers.h"

#include "ff.h"
#include "mass_storage_init.h"

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
		efiPrintf("SD: no card detected");
		return false;
	}

#if HAL_USE_USB_MSD
	// Wait for the USB stack to wake up, or a 5 second timeout, whichever occurs first
	msg_t usbResult = usbConnectedSemaphore.wait(TIME_MS2I(5000));

	bool hasUsb = usbResult == MSG_OK;

	// If we have a device AND USB is connected, mount the card to USB, otherwise
	// mount the null device and try to mount the filesystem ourselves
	if (cardBlockDevice && hasUsb) {
		efiPrintf("SD: USB connected, card handed to mass storage instead of internal logging");

		// Mount the real card to USB
		attachMsdSdCard(cardBlockDevice);

		// At this point we're done: don't try to write files ourselves
		return false;
	}
#endif

	// We were able to connect the SD card, mount the filesystem
	FRESULT err = f_mount(dma_buffers::fs(), "/", 1);
	if (err == FR_OK) {
		efiPrintf("SD card mounted!");
		fs_ready = true;
		return true;
	} else {
		// FatFs error number, not just "it didn't work": FR_NO_FILESYSTEM (13) means the card
		// needs formatting, while FR_DISK_ERR (1) points at the card or the bus instead.
		efiPrintf("SD card failed to mount filesystem, FatFs error %d", err);
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
