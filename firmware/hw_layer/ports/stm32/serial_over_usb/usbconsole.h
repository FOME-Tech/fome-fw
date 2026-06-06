/**
 * @file    usbconsole.h
 *
 * @date Oct 14, 2013
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#pragma once

void usb_serial_start();
bool is_usb_serial_ready();

#if HAL_USE_USB_MSD
void allowUsbEnumeration();
#endif
