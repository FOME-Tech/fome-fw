/*
    ChibiOS - Copyright (C) 2006..2016 Giovanni Di Sirio

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#pragma once

// CDC data endpoint (bulk IN/OUT share endpoint index 2)
#define EFI_USB_CDC_DATA_REQUEST_EP     2
#define EFI_USB_CDC_DATA_AVAILABLE_EP   2
#define EFI_USB_CDC_INTERRUPT_EP        3

#if STM32_USB_USE_OTG1
#define EFI_USB_DRIVER (&USBD1)
#elif STM32_USB_USE_OTG2
#define EFI_USB_DRIVER (&USBD2)
#endif

extern const USBConfig usbcfg;

void usbPopulateSerialNumber(const uint8_t* serialNumber, size_t bytes);
