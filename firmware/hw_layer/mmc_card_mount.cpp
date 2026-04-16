#include "pch.h"

#if EFI_FILE_LOGGING && EFI_PROD_CODE

#include "mmc_card.h"
#include "dma_buffers.h"


#include "ff.h"
#include "mass_storage_init.h"

static bool fs_ready = false;

#if HAL_USE_USB_MSD
static chibios_rt::BinarySemaphore usbConnectedSemaphore(/* taken =*/true);
static chibios_rt::BinarySemaphore usbDisconnectedSemaphore(/* taken =*/true);

void onUsbConnectedNotifyMmcI() {
	// Reset the disconnect semaphore so any USB_EVENT_RESET that fired during
	// enumeration (before SET_CONFIGURATION) doesn't immediately expire the session.
	usbDisconnectedSemaphore.resetI(true);
	usbConnectedSemaphore.signalI();
}

void onUsbDisconnectedNotifyMmcI() {
	usbDisconnectedSemaphore.signalI();
}
#else
void onUsbDisconnectedNotifyMmcI() {}
#endif /* HAL_USE_USB_MSD */

bool isSdCardMounted() {
	return fs_ready;
}

bool mountSdFilesystem() {
	auto cardBlockDevice = initializeMmcBlockDevice();

#if EFI_TUNER_STUDIO
	// If not null, card is present
	engine->outputChannels.sd_present = cardBlockDevice != nullptr;
#endif

	// if no card, don't try to mount FS
	if (!cardBlockDevice) {
		return false;
	}

#if HAL_USE_USB_MSD
	// Wait for the USB stack to wake up, or a 5 second timeout, whichever occurs first
	msg_t usbResult = usbConnectedSemaphore.wait(TIME_MS2I(5000));

	bool hasUsb = usbResult == MSG_OK;

	// If we have a device AND USB is connected, mount the card to USB, then wait
	// for USB to disconnect before reclaiming the card and mounting FatFS.
	if (cardBlockDevice && hasUsb) {
		// Mount the real card to USB
		attachMsdSdCard(cardBlockDevice);
		efiPrintf("SD card attached to USB MSD");

		// Block until USB disconnects (or the device is reset)
		usbDisconnectedSemaphore.wait(TIME_INFINITE);

		// USB is gone — swap LUN 1 back to null device and reclaim the card
		detachMsdSdCard();
		efiPrintf("USB MSD disconnected, remounting SD card as filesystem");
	}
#endif

	// We were able to connect the SD card, mount the filesystem
	FRESULT fres = f_mount(dma_buffers::fs(), "/", 1);
	if (fres == FR_OK) {
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
