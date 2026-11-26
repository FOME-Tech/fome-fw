#include "pch.h"
#include "stored_value_sensor.h"
#include "lps25.h"

static Lps25 device;
static StoredValueSensor baroSensor(SensorType::BarometricPressure, MS2NT(1000));
static StoredValueSensor tempSensor(SensorType::EcuInternalTemperature, MS2NT(1000));

void deinitBaro() {
	// Make way for an analog baro sensor. Otherwise initBaro() keeps the onboard one.
	if (isAdcChannelValid(engineConfiguration->baroSensor.hwChannel)) {
		baroSensor.unregister();
		tempSensor.unregister();
	}
}

// Call after the configured sensors have started
void initBaro() {
	// If there's already an external (analog) baro sensor configured,
	// don't configure the internal one.
	if (Sensor::hasSensor(SensorType::BarometricPressure)) {
		return;
	}

	// Once a try has claimed the pins, later ones return straight away. The pins can't be released,
	// so the device keeps the ones it started with.
	if (!device.hasInit()) {
		device.init(engineConfiguration->lps25BaroSensorScl, engineConfiguration->lps25BaroSensorSda);
	}

	if (device.hasInit()) {
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
