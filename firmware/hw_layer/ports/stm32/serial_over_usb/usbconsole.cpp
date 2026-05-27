/**
 * @file    usbconsole.cpp
 * @brief	USB-over-serial configuration
 *
 * @date Oct 14, 2013
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"

#if EFI_USB_SERIAL

#include "usbconsole.h"
#include "usbcfg.h"
#include "mpu_util.h"

static bool isUsbSerialInitialized = false;

#if HAL_USE_USB_MSD
// When MSD is enabled, defer USB bus connection until the SD card thread
// has finished any WiFi firmware updates and is ready to present media.
static chibios_rt::BinarySemaphore usbEnumerationAllowed(/* taken =*/true);
#endif

/**
 * start USB serial using hard-coded communications pins (see comments inside the code)
 */
void usb_serial_start() {
	usbPopulateSerialNumber(MCU_SERIAL_NUMBER_LOCATION, MCU_SERIAL_NUMBER_BYTES);

	efiSetPadMode("USB DM", EFI_USB_SERIAL_DM, PAL_MODE_ALTERNATE(EFI_USB_AF));
	efiSetPadMode("USB DP", EFI_USB_SERIAL_DP, PAL_MODE_ALTERNATE(EFI_USB_AF));

#if ! EFI_USB_SERIAL_DIRECT
	/*
	 * Initializes a serial-over-USB CDC driver.
	 */
	sduObjectInit(&SDU1);
	sduStart(&SDU1, &serusbcfg);
#endif

	/*
	 * Activates the USB driver and then the USB bus pull-up on D+.
	 * Note, a delay is inserted in order to not have to disconnect the cable
	 * after a reset.
	 */
#if EFI_USB_SERIAL_DIRECT
	USBDriver* usbp = EFI_USB_DRIVER;
#else
	USBDriver* usbp = serusbcfg.usbp;
#endif

// See also https://github.com/rusefi/rusefi/issues/705
#ifndef EFI_SKIP_USB_DISCONNECT
	usbDisconnectBus(usbp);
	chThdSleepMilliseconds(250);
#endif /* EFI_SKIP_USB_DISCONNECT */
	usbStart(usbp, &usbcfg);

#if HAL_USE_USB_MSD
	// Wait for the SD card thread to signal that it's ready
	usbEnumerationAllowed.wait();
#endif

	usbConnectBus(usbp);

	isUsbSerialInitialized = true;
}

void allowUsbEnumeration() {
#if HAL_USE_USB_MSD
	usbEnumerationAllowed.signal();
#endif
}

bool is_usb_serial_ready() {
#if EFI_USB_SERIAL_DIRECT
	return isUsbSerialInitialized && EFI_USB_DRIVER->state == USB_ACTIVE;
#else
	return isUsbSerialInitialized && SDU1.config->usbp->state == USB_ACTIVE;
#endif
}

#else
bool is_usb_serial_ready() {
	return false;
}

#endif /* EFI_USB_SERIAL */
