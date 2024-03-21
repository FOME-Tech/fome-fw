#include "pch.h"

#include "max31855.h"

static StoredValueSensor egts[] = {
	{ SensorType::Egt1, TIME_MS2I(1000) },
	{ SensorType::Egt2, TIME_MS2I(1000) },
	{ SensorType::Egt3, TIME_MS2I(1000) },
	{ SensorType::Egt4, TIME_MS2I(1000) },
	{ SensorType::Egt5, TIME_MS2I(1000) },
	{ SensorType::Egt6, TIME_MS2I(1000) },
	{ SensorType::Egt7, TIME_MS2I(1000) },
	{ SensorType::Egt8, TIME_MS2I(1000) },
};

void initEgt() {
	for (size_t i = 0; i < efi::size(egts); i++) {
		egts[i].Register();
	}

#if EFI_MAX_31855
	if (initMax31855(engineConfiguration->max31855spiDevice, engineConfiguration->max31855_cs)) {
		return;
	}
#endif /* EFI_MAX_31855 */
}
