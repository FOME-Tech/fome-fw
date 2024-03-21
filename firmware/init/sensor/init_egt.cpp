#include "pch.h"

#include "max31855.h"

static StoredValueSensor egts[] = {
	{ SensorType::Egt1, MS2NT(1000) },
	{ SensorType::Egt2, MS2NT(1000) },
	{ SensorType::Egt3, MS2NT(1000) },
	{ SensorType::Egt4, MS2NT(1000) },
	{ SensorType::Egt5, MS2NT(1000) },
	{ SensorType::Egt6, MS2NT(1000) },
	{ SensorType::Egt7, MS2NT(1000) },
	{ SensorType::Egt8, MS2NT(1000) },
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

void setEgt(size_t index, int tempC) {
	if (index >= efi::size(egts)) {
		return;
	}

	egts[index].setValidValue(tempC, getTimeNowNt());
}
