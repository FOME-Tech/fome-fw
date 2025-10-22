#include "pch.h"
#include "stored_value_sensor.h"
#include "lps25.h"

static Lps25 device;
static StoredValueSensor baroSensor(SensorType::BarometricPressure, MS2NT(1000));
static StoredValueSensor tempSensor(SensorType::EcuInternalTemperature, MS2NT(1000));

void initBaro() {
	// If there's already an external (analog) baro sensor configured,
	// don't configure the internal one.
	if (Sensor::hasSensor(SensorType::BarometricPressure)) {
		return;
	}

	if (device.init(engineConfiguration->lps25BaroSensorScl, engineConfiguration->lps25BaroSensorSda)) {
		baroSensor.Register();
		tempSensor.Register();
	}
}

void baroLps25Update() {
#if EFI_PROD_CODE
	if (device.hasInit()) {
		if (auto baro = device.readPressureKpa()) {
			baroSensor.setValidValue(baro.Value, getTimeNowNt());
		}

		if (auto temp = device.readTemperatureC()) {
			tempSensor.setValidValue(temp.Value, getTimeNowNt());
		}
	}
#endif // EFI_PROD_CODE
}
