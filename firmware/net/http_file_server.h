#pragma once

#include "pch.h"

#if EFI_WIFI && EFI_FILE_LOGGING && !defined(EFI_BOOTLOADER)

void startHttpFileServer();

#endif // EFI_WIFI && EFI_FILE_LOGGING && !defined(EFI_BOOTLOADER)
