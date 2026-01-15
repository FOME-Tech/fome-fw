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
		case SensorType::Maf:
			switch (code) {
				case UnexpectedCode::Timeout:      return ObdCode::OBD_Maf_Timeout;
				case UnexpectedCode::Low:          return ObdCode::OBD_Maf_Low;
				case UnexpectedCode::High:         return ObdCode::OBD_Maf_High;
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

static DtcSeverity getSeverityForCode(ObdCode code) {
	const auto& c = engineConfiguration->dtcControl;

	switch (static_cast<uint16_t>(code)) {
		case 0x102: return c.p0102;
		case 0x103: return c.p0103;
		case 0x107: return c.p0107;
		case 0x108: return c.p0108;
		case 0x112: return c.p0112;
		case 0x113: return c.p0113;
		case 0x117: return c.p0117;
		case 0x118: return c.p0118;
		case 0x176: return c.p0176;
		case 0x178: return c.p0178;
		case 0x179: return c.p0179;
		case 0x197: return c.p0197;
		case 0x198: return c.p0198;
		case 0x336: return c.p0336;
		case 0x340: return c.camNoSignal;
		case 0x341: return c.camSyncErrors;
		case 0x345: return c.camNoSignal;
		case 0x346: return c.camSyncErrors;
		case 0x365: return c.camNoSignal;
		case 0x366: return c.camSyncErrors;
		case 0x385: return c.camNoSignal;
		case 0x386: return c.camSyncErrors;
		case 0x522: return c.p0522;
		case 0x523: return c.p0523;
		case 0x327: // falls through
		case 0x332: return c.knockSensorLow;
		default:
			return DtcSeverity::WarningOnly;
	}
}

static void handleCodeSeverity(ObdCode code) {
	// Determine what to do about this particular code
	auto severity = getSeverityForCode(code);
	if (severity != DtcSeverity::Ignore) {
		setError(true, code);
	}
}

static bool check(SensorResult result, ObdCode code, const char* name) {
	// If the sensor is OK, nothing to check.
	if (result) {
		return true;
	}

	if (code != ObdCode::None) {
		warning(code, "Sensor fault: %s %s", name, describeUnexpected(result.Code));

		// Determine what to do about this particular code
		handleCodeSeverity(code);
	} else {
		setError(false, code);
	}

	return false;
}

// Returns true checks on dependent sensors should happen
// (returns false if broken or not configured)
static bool check(SensorType type) {
	// Don't check sensors we don't have
	if (!Sensor::hasSensor(type)) {
		return false;
	}

	SensorResult result = Sensor::get(type);

	// If the sensor is OK, nothing to check.
	if (result) {
		return true;
	}

	ObdCode code = getCode(type, result.Code);

	return check(result, code, Sensor::getSensorName(type));
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

#if EFI_SHAFT_POSITION_INPUT
static void checkTriggerDecoder(TriggerDecoderBase& decoder, ObdCode tooManyErrorsCode) {
	if (decoder.triggerErrorCounter > 50) {
		handleCodeSeverity(tooManyErrorsCode);
	}
}

static void checkCamDecoder(int bank, int cam, const char* name, ObdCode noSignalCode, ObdCode tooManyErrorsCode) {
	{
		int inputIndex = bank * CAMS_PER_BANK + cam;
		if (!isBrainPinValid(engineConfiguration->camInputs[inputIndex])) {
			// No pin configured, skip this cam
			return;
		}
	}

	auto& decoder = engine->triggerCentral.vvtState[bank][cam];

	// scenarios to detect:
	// 1. There is no signal present whatsoever
	//		-> no rising edges counted
	// 2. There are some edges present, but we couldn't sync
	//		-> no VVT position means cam is invalid
	// 3. Some intermittent issue is letting us limp along, but sporadic sync errors are piling up
	//		-> sync error counter is high

	// Scenario 1: No signal at all
	if (decoder.edgeCountRise == 0) {
		handleCodeSeverity(noSignalCode);
		return;
	}

	// Scenario 2: Signal, but no sync
	{
		// If there's no valid VVT position, this hasn't decoded in the last second
		auto vvtResult = engine->triggerCentral.getVVTPosition(bank, cam);
		if (!check(vvtResult, noSignalCode, name)) {
			return;
		}
	}

	// Scenario 3: Pile of sync errors (same check as primary trigger)
	checkTriggerDecoder(decoder, tooManyErrorsCode);
}
#endif // EFI_SHAFT_POSITION_INPUT

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
		if (Sensor::getOrZero(SensorType::BatteryVoltage) < 7.0f) {
			m_timeSinceVbattLow.reset();
		}

		if (!m_ignitionIsOn) {
			// timer keeps track of how long since the state was turned to on (ie, how long ago was it last off)
			m_timeSinceIgnOff.reset();
		}

		// Don't check when:
		// - battery voltage is too low for sensors to work (with stabilization time)
		// - the ignition is off (with stabilization time)
		// TODO: also inhibit checking if we just did a flash burn, since that blocks the ECU for a few seconds.
		bool shouldCheck = m_timeSinceVbattLow.hasElapsedSec(5) && m_timeSinceIgnOff.hasElapsedSec(5);
		m_analogSensorsShouldWork = shouldCheck;
		if (!shouldCheck) {
			return;
		}

#if EFI_PROD_CODE
		// Perform any special board-specific power supply checks
		checkBoardPowerSupply();
#endif
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

#if EFI_SHAFT_POSITION_INPUT
	checkTriggerDecoder(engine->triggerCentral.triggerState, ObdCode::OBD_Crankshaft_Position_Sensor_A_Circuit_SyncErrors);

	// Only check cams if the engine moved recently, AND the primary trigger has 10 syncs
	if (engine->triggerCentral.engineMovedRecently() && engine->triggerCentral.triggerState.crankSynchronizationCounter > 10) {
		checkCamDecoder(
			0, 0,
			"VVT Bank 1 Intake",
			ObdCode::OBD_Camshaft_Position_Sensor_B1I_NoSignal,
			ObdCode::OBD_Camshaft_Position_Sensor_B1I_SyncErrors
		);
		checkCamDecoder(
			0, 1,
			"VVT Bank 1 Exhaust",
			ObdCode::OBD_Camshaft_Position_Sensor_B1E_NoSignal,
			ObdCode::OBD_Camshaft_Position_Sensor_B1E_SyncErrors
		);
		checkCamDecoder(
			1, 0,
			"VVT Bank 2 Intake",
			ObdCode::OBD_Camshaft_Position_Sensor_B2I_NoSignal,
			ObdCode::OBD_Camshaft_Position_Sensor_B2I_SyncErrors
		);
		checkCamDecoder(
			1, 1,
			"VVT Bank 2 Exhaust",
			ObdCode::OBD_Camshaft_Position_Sensor_B2E_NoSignal,
			ObdCode::OBD_Camshaft_Position_Sensor_B2E_SyncErrors
		);
	}
#endif // EFI_SHAFT_POSITION_INPUT

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

#if EFI_SOFTWARE_KNOCK
	// Check for missing knock sensor (signal too low for too long)
	// Only check if knock sensing is enabled and engine is running
	auto knockNoiseTimeout = engineConfiguration->knockNoiseTimeout;
	if (engineConfiguration->enableSoftwareKnock && knockNoiseTimeout > 0 && engine->rpmCalculator.isRunning()) {
		for (size_t i = 0; i < efi::size(m_lastGoodKnockSampleTimer); i++) {
			if (m_lastGoodKnockSampleTimer[i].hasElapsedSec(knockNoiseTimeout)) {
				auto code = i == 0 ? ObdCode::OBD_Knock_Sensor_1_Low : ObdCode::OBD_Knock_Sensor_2_Low;

				handleCodeSeverity(code);
			}
		}
	} else {
		// Not running or knock disabled - reset state
		for (size_t i = 0; i < efi::size(m_lastGoodKnockSampleTimer); i++) {
			m_lastGoodKnockSampleTimer[i].reset();
		}
	}
#endif // EFI_SOFTWARE_KNOCK
}

void SensorChecker::onIgnitionStateChanged(bool ignitionOn) {
	m_ignitionIsOn = ignitionOn;
}

void SensorChecker::onGoodKnockSensorSignal(uint8_t channelIdx, efitick_t knockSenseTime) {
	m_lastGoodKnockSampleTimer[channelIdx].reset(knockSenseTime);
}
