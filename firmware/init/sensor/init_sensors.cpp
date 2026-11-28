/**
 * @file init_sensorss.cpp
 */

#include "pch.h"

#include "init.h"
#include "cli_registry.h"
#include "io_pins.h"

static void initSensorCli();

// The pins initAuxDigital() claimed and their modes, so a burn only touches the ones that changed
static switch_input_pin_e auxDigitalPins[LUA_DIGITAL_INPUT_COUNT];
static pin_input_mode_e auxDigitalModes[LUA_DIGITAL_INPUT_COUNT];

static void initAuxDigital() {
	for (size_t i = 0; i < efi::size(auxDigitalPins); i++) {
		auto pin = engineConfiguration->luaDigitalInputPins[i];

		if (pin == auxDigitalPins[i]) {
			// Already set up like this (or still unassigned)
			continue;
		}

		auxDigitalPins[i] = pin;
		auxDigitalModes[i] = engineConfiguration->luaDigitalInputPinModes[i];

#if EFI_PROD_CODE
		efiSetPadMode("Lua Digital", pin, auxDigitalModes[i]);
#endif // EFI_PROD_CODE
	}
}

static void deInitAuxDigital() {
	for (size_t i = 0; i < efi::size(auxDigitalPins); i++) {
		bool unchanged = auxDigitalPins[i] == engineConfiguration->luaDigitalInputPins[i] &&
						 auxDigitalModes[i] == engineConfiguration->luaDigitalInputPinModes[i];

		if (unchanged) {
			continue;
		}

		brain_pin_markUnused(auxDigitalPins[i]);
		auxDigitalPins[i] = Gpio::Unassigned;
	}
}

void initFixedSensors() {
	// Sensors built in to the board that don't need config
	initBoardSensors();

	// Init CLI functionality for sensors (mocking)
	initSensorCli();

#if defined(HARDWARE_CI) && !defined(HW_PROTEUS)
	chThdSleepMilliseconds(100);

	if (Sensor::getOrZero(SensorType::BatteryVoltage) < 8) {
		// Fake that we have battery voltage, some tests rely on it
		Sensor::setMockValue(SensorType::BatteryVoltage, 10);
	}
#endif
}

void stopConfiguredSensors(const engine_configuration_s* /*prev*/) {
	deinitBaro();
	deinitTps();
	deinitFluidPressure();
	deinitVbatt();
	deinitThermistors();
	deinitLambda();
	deInitFlexSensor();
	deinitAuxSensors();
	deInitVehicleSpeedSensor();
	deinitTurbochargerSpeedSensor();
	deinitMap();
	deinitMaf();
	deinitFuelLevel();
	deinitAuxSpeedSensors();
	deInitAuxDigital();
}

void startConfiguredSensors(const engine_configuration_s* /*prev*/) {
	initVbatt();
	initMap();
	initMaf();
	initFuelLevel();
	initTps();
	initFluidPressure();
	initThermistors();
	initLambda();
	initFlexSensor();
	initAuxSensors();
	initVehicleSpeedSensor();
	initTurbochargerSpeedSensor();
	initAuxSpeedSensors();
	initAuxDigital();

	// Only if no other baro sensor was configured
	initBaro();
}

// Mocking/testing helpers
static void initSensorCli() {
	addConsoleActionSS("set_sensor_mock", [](const char* typeName, const char* valueStr) {
		SensorType type = findSensorTypeByName(typeName);

		if (type == SensorType::Invalid) {
			efiPrintf("Invalid sensor type specified: %s", typeName);
			return;
		}

		float value = atoff(valueStr);

		Sensor::setMockValue(type, value);
	});

	addConsoleAction("reset_sensor_mocks", Sensor::resetAllMocks);
	addConsoleAction("show_sensors", Sensor::showAllSensorInfo);
	addConsoleActionI("show_sensor", [](int idx) { Sensor::showInfo(static_cast<SensorType>(idx)); });
}
