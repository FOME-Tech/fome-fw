#include "pch.h"

#include "malfunction_central.h"

// Decode what OBD code we should use for a particular [sensor, code] problem
static ObdCode getCode(SensorType type, UnexpectedCode code) {
	switch (type) {
		case SensorType::Tps1:
		case SensorType::Tps1Primary:
			switch (code) {
				case UnexpectedCode::Timeout:
					return ObdCode::OBD_TPS1_Primary_Timeout;
				case UnexpectedCode::Low:
					return ObdCode::OBD_TPS1_Primary_Low;
				case UnexpectedCode::High:
					return ObdCode::OBD_TPS1_Primary_High;
				case UnexpectedCode::Inconsistent:
					return ObdCode::OBD_TPS1_Correlation;
				default:
					break;
			}
			break;
		case SensorType::Tps1Secondary:
			switch (code) {
				case UnexpectedCode::Timeout:
					return ObdCode::OBD_TPS1_Secondary_Timeout;
				case UnexpectedCode::Low:
					return ObdCode::OBD_TPS1_Secondary_Low;
				case UnexpectedCode::High:
					return ObdCode::OBD_TPS1_Secondary_High;
				default:
					break;
			}
			break;
		case SensorType::Tps2:
		case SensorType::Tps2Primary:
			switch (code) {
				case UnexpectedCode::Timeout:
					return ObdCode::OBD_TPS2_Primary_Timeout;
				case UnexpectedCode::Low:
					return ObdCode::OBD_TPS2_Primary_Low;
				case UnexpectedCode::High:
					return ObdCode::OBD_TPS2_Primary_High;
				case UnexpectedCode::Inconsistent:
					return ObdCode::OBD_TPS2_Correlation;
				default:
					break;
			}
			break;
		case SensorType::Tps2Secondary:
			switch (code) {
				case UnexpectedCode::Timeout:
					return ObdCode::OBD_TPS2_Secondary_Timeout;
				case UnexpectedCode::Low:
					return ObdCode::OBD_TPS2_Secondary_Low;
				case UnexpectedCode::High:
					return ObdCode::OBD_TPS2_Secondary_High;
				default:
					break;
			}
			break;

		case SensorType::AcceleratorPedal:
		case SensorType::AcceleratorPedalPrimary:
			switch (code) {
				case UnexpectedCode::Timeout:
					return ObdCode::OBD_PPS_Primary_Timeout;
				case UnexpectedCode::Low:
					return ObdCode::OBD_PPS_Primary_Low;
				case UnexpectedCode::High:
					return ObdCode::OBD_PPS_Primary_High;
				case UnexpectedCode::Inconsistent:
					return ObdCode::OBD_PPS_Correlation;
				default:
					break;
			}
			break;
		case SensorType::AcceleratorPedalSecondary:
			switch (code) {
				case UnexpectedCode::Timeout:
					return ObdCode::OBD_PPS_Secondary_Timeout;
				case UnexpectedCode::Low:
					return ObdCode::OBD_PPS_Secondary_Low;
				case UnexpectedCode::High:
					return ObdCode::OBD_PPS_Secondary_High;
				default:
					break;
			}
			break;

		case SensorType::MapSlow:
		case SensorType::MapSlow2:
			switch (code) {
				case UnexpectedCode::Timeout:
					return ObdCode::OBD_Map_Timeout;
				case UnexpectedCode::Low:
					return ObdCode::OBD_Map_Low;
				case UnexpectedCode::High:
					return ObdCode::OBD_Map_High;
				default:
					break;
			}
			break;
		case SensorType::Maf:
			switch (code) {
				case UnexpectedCode::Timeout:
					return ObdCode::OBD_Maf_Timeout;
				case UnexpectedCode::Low:
					return ObdCode::OBD_Maf_Low;
				case UnexpectedCode::High:
					return ObdCode::OBD_Maf_High;
				default:
					break;
			}
			break;
		case SensorType::Clt:
			switch (code) {
				case UnexpectedCode::Timeout:
					return ObdCode::OBD_Clt_Timeout;
				case UnexpectedCode::Low:
					return ObdCode::OBD_Clt_Low;
				case UnexpectedCode::High:
					return ObdCode::OBD_Clt_High;
				default:
					break;
			}
			break;
		case SensorType::Iat:
			switch (code) {
				case UnexpectedCode::Timeout:
					return ObdCode::OBD_Iat_Timeout;
				case UnexpectedCode::Low:
					return ObdCode::OBD_Iat_Low;
				case UnexpectedCode::High:
					return ObdCode::OBD_Iat_High;
				default:
					break;
			}
			break;
		case SensorType::FuelEthanolPercent:
			switch (code) {
				case UnexpectedCode::Timeout:
					return ObdCode::OBD_FlexSensor_Timeout;
				case UnexpectedCode::Low:
					return ObdCode::OBD_FlexSensor_Low;
				case UnexpectedCode::High:
					return ObdCode::OBD_FlexSensor_High;
				default:
					break;
			}
			break;
		case SensorType::OilPressure:
			switch (code) {
				case UnexpectedCode::Timeout:
					return ObdCode::OBD_OilP_Timeout;
				case UnexpectedCode::Low:
					return ObdCode::OBD_OilP_Low;
				case UnexpectedCode::High:
					return ObdCode::OBD_OilP_High;
				default:
					break;
			}
			break;
		case SensorType::OilTemperature:
			switch (code) {
				case UnexpectedCode::Timeout:
					return ObdCode::OBD_OilT_Timeout;
				case UnexpectedCode::Low:
					return ObdCode::OBD_OilT_Low;
				case UnexpectedCode::High:
					return ObdCode::OBD_OilT_High;
				default:
					break;
			}
			break;
		default:
			break;
	}

	return ObdCode::None;
}

inline const char* describeUnexpected(UnexpectedCode code) {
	switch (code) {
		case UnexpectedCode::Timeout:
			return "has timed out";
		case UnexpectedCode::High:
			return "input too high";
		case UnexpectedCode::Low:
			return "input too low";
		case UnexpectedCode::Inconsistent:
			return "is inconsistent";
		case UnexpectedCode::Configuration:
			return "is misconfigured";
		case UnexpectedCode::Unknown:
		default:
			return "unknown";
	}
}

static DtcSeverity getSeverityForCode(ObdCode code) {
	const auto& c = engineConfiguration->dtcControl;

	switch (static_cast<uint16_t>(code)) {
		case 0x102:
			return c.p0102;
		case 0x103:
			return c.p0103;
		case 0x107:
			return c.p0107;
		case 0x108:
			return c.p0108;
		case 0x112:
			return c.p0112;
		case 0x113:
			return c.p0113;
		case 0x117:
			return c.p0117;
		case 0x118:
			return c.p0118;
		case 0x176:
			return c.p0176;
		case 0x178:
			return c.p0178;
		case 0x179:
			return c.p0179;
		case 0x197:
			return c.p0197;
		case 0x198:
			return c.p0198;
		case 0x336:
			return c.p0336;
		case 0x340:
			return c.camNoSignal;
		case 0x341:
			return c.camSyncErrors;
		case 0x345:
			return c.camNoSignal;
		case 0x346:
			return c.camSyncErrors;
		case 0x365:
			return c.camNoSignal;
		case 0x366:
			return c.camSyncErrors;
		case 0x385:
			return c.camNoSignal;
		case 0x386:
			return c.camSyncErrors;
		case 0x522:
			return c.p0522;
		case 0x523:
			return c.p0523;
		case 0x327: // falls through
		case 0x332:
			return c.knockSensorLow;
		default:
			return DtcSeverity::WarningOnly;
	}
}

bool DtcDebouncer::report(ObdCode code) {
	Entry* freeSlot = nullptr;

	for (size_t i = 0; i < efi::size(m_entries); i++) {
		Entry& e = m_entries[i];

		if (e.code == code) {
			e.seenThisCycle = true;

			if (e.count < threshold) {
				e.count++;
			}

			return e.count >= threshold;
		}

		if (e.code == ObdCode::None && !freeSlot) {
			freeSlot = &e;
		}
	}

	if (!freeSlot) {
		// Out of room to track this one. Report it immediately rather than
		// silently swallowing what may be a real fault.
		return true;
	}

	freeSlot->code = code;
	freeSlot->count = 1;
	freeSlot->seenThisCycle = true;

	return freeSlot->count >= threshold;
}

void DtcDebouncer::endCycle() {
	for (size_t i = 0; i < efi::size(m_entries); i++) {
		Entry& e = m_entries[i];

		if (e.code == ObdCode::None) {
			continue;
		}

		if (!e.seenThisCycle) {
			// Not faulted this time around, leak back down
			e.count--;

			if (e.count == 0) {
				e.code = ObdCode::None;
			}
		}

		e.seenThisCycle = false;
	}
}

void DtcDebouncer::reset() {
	for (size_t i = 0; i < efi::size(m_entries); i++) {
		m_entries[i] = {};
	}
}

static void handleCodeSeverity(DtcDebouncer& debounce, ObdCode code) {
	// Determine what to do about this particular code
	auto severity = getSeverityForCode(code);
	if (severity == DtcSeverity::Ignore) {
		return;
	}

	// Only latch the code once the fault has stuck around long enough to be believable
	if (debounce.report(code)) {
		setError(true, code);
	}
}

static bool check(DtcDebouncer& debounce, SensorResult result, ObdCode code, const char* name) {
	// If the sensor is OK, nothing to check.
	if (result) {
		return true;
	}

	if (code != ObdCode::None) {
		warning(code, "Sensor fault: %s %s", name, describeUnexpected(result.Code));

		// Determine what to do about this particular code
		handleCodeSeverity(debounce, code);
	}

	return false;
}

// Returns true checks on dependent sensors should happen
// (returns false if broken or not configured)
static bool check(DtcDebouncer& debounce, SensorType type) {
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

	return check(debounce, result, code, Sensor::getSensorName(type));
}

#if BOARD_EXT_GPIOCHIPS > 0 && EFI_PROD_CODE
// ObdCode values are hex-encoded P-codes, so they don't run contiguously past circuit 9
// (P0209 is 0x209, but P0210 is 0x210). Look the code up instead of doing arithmetic on it.
static ObdCode getCodeForInjector(size_t idx, brain_pin_diag_e diag) {
	static constexpr ObdCode codes[] = {
			ObdCode::OBD_Injector_Circuit_1,
			ObdCode::OBD_Injector_Circuit_2,
			ObdCode::OBD_Injector_Circuit_3,
			ObdCode::OBD_Injector_Circuit_4,
			ObdCode::OBD_Injector_Circuit_5,
			ObdCode::OBD_Injector_Circuit_6,
			ObdCode::OBD_Injector_Circuit_7,
			ObdCode::OBD_Injector_Circuit_8,
			ObdCode::OBD_Injector_Circuit_9,
			ObdCode::OBD_Injector_Circuit_10,
			ObdCode::OBD_Injector_Circuit_11,
			ObdCode::OBD_Injector_Circuit_12,
	};

	if (idx >= efi::size(codes)) {
		return ObdCode::None;
	}

	// TODO: do something more intelligent with `diag`?
	UNUSED(diag);

	return codes[idx];
}

static ObdCode getCodeForIgnition(size_t idx, brain_pin_diag_e diag) {
	static constexpr ObdCode codes[] = {
			ObdCode::OBD_Ignition_Circuit_1,
			ObdCode::OBD_Ignition_Circuit_2,
			ObdCode::OBD_Ignition_Circuit_3,
			ObdCode::OBD_Ignition_Circuit_4,
			ObdCode::OBD_Ignition_Circuit_5,
			ObdCode::OBD_Ignition_Circuit_6,
			ObdCode::OBD_Ignition_Circuit_7,
			ObdCode::OBD_Ignition_Circuit_8,
			ObdCode::OBD_Ignition_Circuit_9,
			ObdCode::OBD_Ignition_Circuit_10,
			ObdCode::OBD_Ignition_Circuit_11,
			ObdCode::OBD_Ignition_Circuit_12,
	};

	if (idx >= efi::size(codes)) {
		return ObdCode::None;
	}

	// TODO: do something more intelligent with `diag`?
	UNUSED(diag);

	return codes[idx];
}
#endif // BOARD_EXT_GPIOCHIPS > 0 && EFI_PROD_CODE

#if EFI_SHAFT_POSITION_INPUT
static void checkTriggerDecoder(DtcDebouncer& debounce, TriggerDecoderBase& decoder, ObdCode tooManyErrorsCode) {
	if (decoder.triggerErrorCounter > 50) {
		handleCodeSeverity(debounce, tooManyErrorsCode);
	}
}

static void checkCamDecoder(
		DtcDebouncer& debounce, int bank, int cam, const char* name, ObdCode noSignalCode, ObdCode tooManyErrorsCode) {
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
	if (!decoder.hasSignal) {
		handleCodeSeverity(debounce, noSignalCode);
		return;
	}

	// Scenario 2: Signal, but no sync
	{
		// If there's no valid VVT position, this hasn't decoded in the last second
		auto vvtResult = engine->triggerCentral.getVVTPosition(bank, cam);
		if (!check(debounce, vvtResult, noSignalCode, name)) {
			return;
		}
	}

	// Scenario 3: Pile of sync errors (same check as primary trigger)
	checkTriggerDecoder(debounce, decoder, tooManyErrorsCode);
}

static void checkTriggers(DtcDebouncer& debounce, bool isStopped, bool isRunning, float rpm) {
	// Nothing to check if the engine is stopped
	if (isStopped) {
		return;
	}

	// If the engine is running but below cranking RPM threshold, disable trigger checking.
	// It may be about to stop, so don't worry about anything that goes wrong.
	if (isRunning && rpm < engineConfiguration->cranking.rpm) {
		return;
	}

	checkTriggerDecoder(
			debounce,
			engine->triggerCentral.triggerState,
			ObdCode::OBD_Crankshaft_Position_Sensor_A_Circuit_SyncErrors);

	// Only check cams if the engine moved recently, AND the primary trigger has 20 syncs
	if (engine->triggerCentral.triggerState.crankSynchronizationCounter > 20) {
		checkCamDecoder(
				debounce,
				0,
				0,
				"VVT Bank 1 Intake",
				ObdCode::OBD_Camshaft_Position_Sensor_B1I_NoSignal,
				ObdCode::OBD_Camshaft_Position_Sensor_B1I_SyncErrors);
		checkCamDecoder(
				debounce,
				0,
				1,
				"VVT Bank 1 Exhaust",
				ObdCode::OBD_Camshaft_Position_Sensor_B1E_NoSignal,
				ObdCode::OBD_Camshaft_Position_Sensor_B1E_SyncErrors);
		checkCamDecoder(
				debounce,
				1,
				0,
				"VVT Bank 2 Intake",
				ObdCode::OBD_Camshaft_Position_Sensor_B2I_NoSignal,
				ObdCode::OBD_Camshaft_Position_Sensor_B2I_SyncErrors);
		checkCamDecoder(
				debounce,
				1,
				1,
				"VVT Bank 2 Exhaust",
				ObdCode::OBD_Camshaft_Position_Sensor_B2E_NoSignal,
				ObdCode::OBD_Camshaft_Position_Sensor_B2E_SyncErrors);
	}
}
#endif // EFI_SHAFT_POSITION_INPUT

bool SensorChecker::shouldCheckSensors() {
	bool hasBeenRunningOneSecond = getTimeNowNt() > MS2NT(1000);
	if (!hasBeenRunningOneSecond) {
		// Don't bother checking ANYTHING for a second to allow power to stabilize
		return false;
	}

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
	// This applies on every board, including those that monitor their own sensor supply: the ECU
	// can be alive on USB power with the key off, and the sensors it would be checking have no
	// power at all. Anything they report in that state is meaningless.
	// TODO: also inhibit checking if we just did a flash burn, since that blocks the ECU for a few seconds.
	if (!m_timeSinceVbattLow.hasElapsedSec(5) || !m_timeSinceIgnOff.hasElapsedSec(5)) {
		return false;
	}

	if (Sensor::hasSensor(SensorType::Sensor5vVoltage)) {
		float sensorSupply = Sensor::getOrZero(SensorType::Sensor5vVoltage);

		// Inhibit checking if the sensor supply isn't OK, but register a warning for that instead
		if (sensorSupply > 5.25f) {
			warning(ObdCode::Sensor5vSupplyHigh, "5V sensor supply high: %.2f", sensorSupply);
			setError(true, ObdCode::Sensor5vSupplyHigh);
			return false;
		} else if (sensorSupply < 4.75f) {
			warning(ObdCode::Sensor5vSupplyLow, "5V sensor supply low: %.2f", sensorSupply);
			setError(true, ObdCode::Sensor5vSupplyLow);
			return false;
		} else {
			setError(false, ObdCode::Sensor5vSupplyHigh);
			setError(false, ObdCode::Sensor5vSupplyLow);
		}
	}

#if EFI_PROD_CODE
	// Perform any special board-specific power supply checks
	checkBoardPowerSupply();
#endif

	return true;
}

void SensorChecker::onSlowCallback() {
	m_analogSensorsShouldWork = shouldCheckSensors();

	if (!m_analogSensorsShouldWork) {
		// Whatever we'd counted up doesn't mean anything now that we know the sensors
		// may have been unpowered - start over when checking resumes.
		m_dtcDebounce.reset();
		return;
	}

	auto& debounce = m_dtcDebounce;

	// Check sensors
	bool tps1DependenciesOk = check(debounce, SensorType::Tps1Primary);
	if (Sensor::isRedundant(SensorType::Tps1)) {
		tps1DependenciesOk &= check(debounce, SensorType::Tps1Secondary);

		if (tps1DependenciesOk) {
			// Both pri/sec sensors are OK, check the combined sensor
			check(debounce, SensorType::Tps1);
		}
	}

	bool tps2DependenciesOk = check(debounce, SensorType::Tps2Primary);
	if (Sensor::isRedundant(SensorType::Tps2)) {
		tps2DependenciesOk &= check(debounce, SensorType::Tps2Secondary);

		if (tps2DependenciesOk) {
			// Both pri/sec sensors are OK, check the combined sensor
			check(debounce, SensorType::Tps2);
		}
	}

	if (check(debounce, SensorType::AcceleratorPedalPrimary) &&
		check(debounce, SensorType::AcceleratorPedalSecondary)) {
		check(debounce, SensorType::AcceleratorPedal);
	}

	check(debounce, SensorType::MapSlow);
	check(debounce, SensorType::MapSlow2);

	check(debounce, SensorType::Clt);
	check(debounce, SensorType::Iat);

	check(debounce, SensorType::FuelEthanolPercent);

	check(debounce, SensorType::OilPressure);
	check(debounce, SensorType::OilTemperature);

	bool isStopped = engine->rpmCalculator.isStopped();
	bool isRunning = engine->rpmCalculator.isRunning();
	float rpm = Sensor::getOrZero(SensorType::Rpm);

#if EFI_SHAFT_POSITION_INPUT
	checkTriggers(debounce, isStopped, isRunning, rpm);
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
			auto code = getCodeForInjector(i, diag);

			char description[32];
			pinDiag2string(description, efi::size(description), diag);
			warning(code, "Injector %d fault: %s", i + 1, description);
			handleCodeSeverity(debounce, code);

			anyInjectorHasProblem |= true;
		}
	}

	// Check ignition
	bool anyIgnHasProblem = false;
	for (size_t i = 0; i < efi::size(enginePins.coils); i++) {
		IgnitionOutputPin& pin = enginePins.coils[i];

		// Skip not-configured pins
		if (!pin.isInitialized()) {
			continue;
		}

		auto diag = pin.getDiag();
		if (diag != PIN_OK && diag != PIN_INVALID) {
			auto code = getCodeForIgnition(i, diag);

			char description[32];
			pinDiag2string(description, efi::size(description), diag);
			warning(code, "Ignition %d fault: %s", i + 1, description);
			handleCodeSeverity(debounce, code);

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
	if (engineConfiguration->enableSoftwareKnock && knockNoiseTimeout > 0 && isRunning) {
		for (size_t i = 0; i < efi::size(m_lastGoodKnockSampleTimer); i++) {
			if (m_hasSeenKnockSensor[i] && m_lastGoodKnockSampleTimer[i].hasElapsedSec(knockNoiseTimeout)) {
				auto code = i == 0 ? ObdCode::OBD_Knock_Sensor_1_Low : ObdCode::OBD_Knock_Sensor_2_Low;

				handleCodeSeverity(debounce, code);
			}
		}
	} else {
		// Not running or knock disabled - reset state
		for (size_t i = 0; i < efi::size(m_lastGoodKnockSampleTimer); i++) {
			m_lastGoodKnockSampleTimer[i].reset();
			m_hasSeenKnockSensor[i] = false;
		}
	}
#endif // EFI_SOFTWARE_KNOCK

	// Age out any code that wasn't reported on this pass
	debounce.endCycle();
}

void SensorChecker::onIgnitionStateChanged(bool ignitionOn) {
	m_ignitionIsOn = ignitionOn;
}

void SensorChecker::onKnockSensorSignal(float dbv, uint8_t channelIdx, efitick_t knockSenseTime) {
	m_hasSeenKnockSensor[channelIdx] = true;

	// Track when we last had a reasonable signal level (for sensor disconnect detection)
	// A working knock sensor typically reads -50 to -20 dBv during normal operation
	// A disconnected sensor reads essentially noise floor, below -80 dBv
	if (dbv > engineConfiguration->knockNoiseThreshold) {
		m_lastGoodKnockSampleTimer[channelIdx].reset(knockSenseTime);
	}
}
