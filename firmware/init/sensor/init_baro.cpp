#include "pch.h"
#include "Lps25Sensor.h"

static Lps25 device;
static Lps25Sensor sensor(device);
#ifdef STM32H7XX
static Lps25TempSensor sensor2(device);
#endif

void initBaro() {
	// If there's already an external (analog) baro sensor configured,
	// don't configure the internal one.
	if (Sensor::hasSensor(SensorType::BarometricPressure)) {
		return;
	}

	if (device.init(engineConfiguration->lps25BaroSensorScl, engineConfiguration->lps25BaroSensorSda)) {
		sensor.Register();
	}
}

#ifdef STM32H7XX
void initInteralLpsTemp() {
	
	if (device.init(engineConfiguration->lps25BaroSensorScl, engineConfiguration->lps25BaroSensorSda)) {
		sensor2.Register();
	}
}

void tempLps25Update() {
	#if EFI_PROD_CODE
		if (device.hasInit()) {
			sensor2.update();
		}
	#endif // EFI_PROD_CODE
}
#endif

void baroLps25Update() {
#if EFI_PROD_CODE
	if (device.hasInit()) {
		sensor.update();
	}
#endif // EFI_PROD_CODE
}
