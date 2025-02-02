#include "pch.h"

#include "malfunction_central.h"

// Decode what OBD code we should use for a particular [sensor, code] problem
static ObdCode getCode(SensorType type, UnexpectedCode code) {
	switch (type) {
		case SensorType::Tps1:
		case SensorType::Tps1Primary:
			switch (code) {
				case UnexpectedCode::Timeout:      return ObdCode::OBD_TPS1_Primary_Timeout;
				case UnexpectedCode::Low:          return ObdCode::OBD_TPS1_Primary_Low;
				case UnexpectedCode::High:         return ObdCode::OBD_TPS1_Primary_High;
				case UnexpectedCode::Inconsistent: return ObdCode::OBD_TPS1_Correlation;
				default: break;
			} break;
		case SensorType::Tps1Secondary:
			switch (code) {
				case UnexpectedCode::Timeout:      return ObdCode::OBD_TPS1_Secondary_Timeout;
				case UnexpectedCode::Low:          return ObdCode::OBD_TPS1_Secondary_Low;
				case UnexpectedCode::High:         return ObdCode::OBD_TPS1_Secondary_High;
				default: break;
			} break;
		case SensorType::Tps2:
		case SensorType::Tps2Primary:
			switch (code) {
				case UnexpectedCode::Timeout:      return ObdCode::OBD_TPS2_Primary_Timeout;
				case UnexpectedCode::Low:          return ObdCode::OBD_TPS2_Primary_Low;
				case UnexpectedCode::High:         return ObdCode::OBD_TPS2_Primary_High;
				case UnexpectedCode::Inconsistent: return ObdCode::OBD_TPS2_Correlation;
				default: break;
			} break;
		case SensorType::Tps2Secondary:
			switch (code) {
				case UnexpectedCode::Timeout:      return ObdCode::OBD_TPS2_Secondary_Timeout;
				case UnexpectedCode::Low:          return ObdCode::OBD_TPS2_Secondary_Low;
				case UnexpectedCode::High:         return ObdCode::OBD_TPS2_Secondary_High;
				default: break;
			} break;

		case SensorType::AcceleratorPedal:
		case SensorType::AcceleratorPedalPrimary:
			switch (code) {
				case UnexpectedCode::Timeout:      return ObdCode::OBD_PPS_Primary_Timeout;
				case UnexpectedCode::Low:          return ObdCode::OBD_PPS_Primary_Low;
				case UnexpectedCode::High:         return ObdCode::OBD_PPS_Primary_High;
				case UnexpectedCode::Inconsistent: return ObdCode::OBD_PPS_Correlation;
				default: break;
			} break;
		case SensorType::AcceleratorPedalSecondary:
			switch (code) {
				case UnexpectedCode::Timeout:      return ObdCode::OBD_PPS_Secondary_Timeout;
				case UnexpectedCode::Low:          return ObdCode::OBD_PPS_Secondary_Low;
				case UnexpectedCode::High:         return ObdCode::OBD_PPS_Secondary_High;
				default: break;
			} break;

		case SensorType::MapSlow:
		case SensorType::MapSlow2:
			switch (code) {
				case UnexpectedCode::Timeout:      return ObdCode::OBD_Map_Timeout;
				case UnexpectedCode::Low:          return ObdCode::OBD_Map_Low;
				case UnexpectedCode::High:         return ObdCode::OBD_Map_High;
				default: break;
			} break;
		case SensorType::Clt:
			switch (code) {
				case UnexpectedCode::Timeout:      return ObdCode::OBD_Clt_Timeout;
				case UnexpectedCode::Low:          return ObdCode::OBD_Clt_Low;
				case UnexpectedCode::High:         return ObdCode::OBD_Clt_High;
				default: break;
			} break;
		case SensorType::Iat:
			switch (code) {
				case UnexpectedCode::Timeout:      return ObdCode::OBD_Iat_Timeout;
				case UnexpectedCode::Low:          return ObdCode::OBD_Iat_Low;
				case UnexpectedCode::High:         return ObdCode::OBD_Iat_High;
				default: break;
			} break;
		case SensorType::FuelEthanolPercent:
			switch (code) {
				case UnexpectedCode::Timeout:      return ObdCode::OBD_FlexSensor_Timeout;
				case UnexpectedCode::Low:          return ObdCode::OBD_FlexSensor_Low;
				case UnexpectedCode::High:         return ObdCode::OBD_FlexSensor_High;
				default: break;
			} break;
		case SensorType::OilPressure:
			switch (code) {
				case UnexpectedCode::Timeout:      return ObdCode::OBD_OilP_Timeout;
				case UnexpectedCode::Low:          return ObdCode::OBD_OilP_Low;
				case UnexpectedCode::High:         return ObdCode::OBD_OilP_High;
				default: break;
			} break;
		case SensorType::OilTemperature:
			switch (code) {
				case UnexpectedCode::Timeout:      return ObdCode::OBD_OilT_Timeout;
				case UnexpectedCode::Low:          return ObdCode::OBD_OilT_Low;
				case UnexpectedCode::High:         return ObdCode::OBD_OilT_High;
				default: break;
			} break;
		default:
			break;
	}

	return ObdCode::None;
}

inline const char* describeUnexpected(UnexpectedCode code) {
	switch (code) {
		case UnexpectedCode::Timeout: return "has timed out";
		case UnexpectedCode::High: return "input too high";
		case UnexpectedCode::Low: return "input too low";
		case UnexpectedCode::Inconsistent: return "is inconsistent";
		case UnexpectedCode::Configuration: return "is misconfigured";
		case UnexpectedCode::Unknown:
		default:
			return "unknown";
	}
}

// Returns true checks on dependent sensors should happen
// (returns false if broken or not configured)
static bool check(SensorType type) {
	// Don't check sensors we don't have
	if (!Sensor::hasSensor(type)) {
		return false;
	}

	auto result = Sensor::get(type);

	// If the sensor is OK, nothing to check.
	if (result) {
		return true;
	}

	ObdCode code = getCode(type, result.Code);

	if (code != ObdCode::None) {
		warning(code, "Sensor fault: %s %s", Sensor::getSensorName(type), describeUnexpected(result.Code));
		setError(true, code);
	} else {
		setError(false, code);
	}

	return false;
}

#if BOARD_EXT_GPIOCHIPS > 0 && EFI_PROD_CODE
static ObdCode getCodeForInjector(int idx, brain_pin_diag_e diag) {
	if (idx < 0 || idx >= 12) {
		return ObdCode::None;
	}

	// TODO: do something more intelligent with `diag`?
	UNUSED(diag);

	return (ObdCode)((int)ObdCode::OBD_Injector_Circuit_1 + idx);
}

static ObdCode getCodeForIgnition(int idx, brain_pin_diag_e diag) {
	if (idx < 0 || idx >= 12) {
		return ObdCode::None;
	}

	// TODO: do something more intelligent with `diag`?
	UNUSED(diag);

	return (ObdCode)((int)ObdCode::OBD_Ignition_Circuit_1 + idx);
}
#endif // BOARD_EXT_GPIOCHIPS > 0 && EFI_PROD_CODE

void SensorChecker::onSlowCallback() {
	if (Sensor::hasSensor(SensorType::Sensor5vVoltage)) {
		float sensorSupply = Sensor::getOrZero(SensorType::Sensor5vVoltage);

		// Inhibit checking if the sensor supply isn't OK, but register a warning for that instead
		if (sensorSupply > 5.25f) {
			warning(ObdCode::Sensor5vSupplyHigh, "5V sensor supply high: %.2f", sensorSupply);
			setError(true, ObdCode::Sensor5vSupplyHigh);
			return;
		} else if (sensorSupply < 4.75f) {
			warning(ObdCode::Sensor5vSupplyLow, "5V sensor supply low: %.2f", sensorSupply);
			setError(true, ObdCode::Sensor5vSupplyLow);
			return;
		} else {
			setError(false, ObdCode::Sensor5vSupplyHigh);
			setError(false, ObdCode::Sensor5vSupplyLow);
		}

	} else {
		bool batteryVoltageSufficient = Sensor::getOrZero(SensorType::BatteryVoltage) > 7.0f;

		// Don't check when:
		// - battery voltage is too low for sensors to work
		// - the ignition is off
		// - ignition was just turned on (let things stabilize first)
		// TODO: also inhibit checking if we just did a flash burn, since that blocks the ECU for a few seconds.
		bool shouldCheck = batteryVoltageSufficient && m_ignitionIsOn && m_timeSinceIgnOff.hasElapsedSec(5);
		m_analogSensorsShouldWork = shouldCheck;
		if (!shouldCheck) {
			return;
		}
	}

	// Check sensors
	bool tps1DependenciesOk = check(SensorType::Tps1Primary);

	if (Sensor::isRedundant(SensorType::Tps1)) {
		tps1DependenciesOk &= check(SensorType::Tps1Secondary);

		if (tps1DependenciesOk) {
			// Both pri/sec sensors are OK, check the combined sensor
			check(SensorType::Tps1);
		}
	}

	bool tps2DependenciesOk = check(SensorType::Tps2Primary);
	if (Sensor::isRedundant(SensorType::Tps2)) {
		tps2DependenciesOk &= check(SensorType::Tps2Secondary);

		if (tps2DependenciesOk) {
			// Both pri/sec sensors are OK, check the combined sensor
			check(SensorType::Tps2);
		}
	}

	if (check(SensorType::AcceleratorPedalPrimary) && check(SensorType::AcceleratorPedalSecondary)) {
		check(SensorType::AcceleratorPedal);
	}

	check(SensorType::MapSlow);
	check(SensorType::MapSlow2);

	check(SensorType::Clt);
	check(SensorType::Iat);

	check(SensorType::FuelEthanolPercent);

	check(SensorType::OilPressure);
	check(SensorType::OilTemperature);

// only bother checking these if we have GPIO chips actually capable of reporting an error
#if BOARD_EXT_GPIOCHIPS > 0 && EFI_PROD_CODE
	// Check injectors
	bool anyInjectorHasProblem = false;

	for (size_t i = 0; i < efi::size(enginePins.injectors); i++) {
		InjectorOutputPin& pin = enginePins.injectors[i];

		// Skip not-configured pins
		if (!pin.isInitialized()) {
			continue;
		}

		auto diag = pin.getDiag();
		if (diag != PIN_OK && diag != PIN_INVALID) {
			auto code = getCodeForInjector(i + 1, diag);

			char description[32];
			pinDiag2string(description, efi::size(description), diag);
			warning(code, "Injector %d fault: %s", i, description);
			setError(true, code);

			anyInjectorHasProblem |= true;
		}
	}

	// Check ignition
	bool anyIgnHasProblem = false;
	for (size_t i = 0; i < efi::size(enginePins.injectors); i++) {
		IgnitionOutputPin& pin = enginePins.coils[i];

		// Skip not-configured pins
		if (!pin.isInitialized()) {
			continue;
		}

		auto diag = pin.getDiag();
		if (diag != PIN_OK && diag != PIN_INVALID) {
			auto code = getCodeForIgnition(i + 1, diag);

			char description[32];
			pinDiag2string(description, efi::size(description), diag);
			warning(code, "Ignition %d fault: %s", i, description);
			setError(true, code);

			anyIgnHasProblem |= true;
		}
	}

	engine->outputChannels.injectorFault = anyInjectorHasProblem;
	engine->outputChannels.ignitionFault = anyIgnHasProblem;
#endif // BOARD_EXT_GPIOCHIPS > 0
}

void SensorChecker::onIgnitionStateChanged(bool ignitionOn) {
	m_ignitionIsOn = ignitionOn;

	if (!ignitionOn) {
		// timer keeps track of how long since the state was turned to on (ie, how long ago was it last off)
		m_timeSinceIgnOff.reset();
	}
}
