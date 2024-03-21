#include "pch.h"

#include "max31855.h"

void initEgt() {
#if EFI_MAX_31855
	if (initMax31855(engineConfiguration->max31855spiDevice, engineConfiguration->max31855_cs)) {
		return;
	}
#endif /* EFI_MAX_31855 */
}
