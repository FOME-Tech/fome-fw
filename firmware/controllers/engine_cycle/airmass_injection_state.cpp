#include "pch.h"
#include "airmass_injection_state.h"
#include "airmass_loads.h"

static bool isComposite(engine_load_mode_e mode) {
	return mode == LM_SD_ALPHA_N;
}

static bool needsStandalonePublication(engine_load_mode_e mode) {
	return mode == LM_SPEED_DENSITY || mode == LM_ALPHA_N || mode == LM_REAL_MAF;
}

bool AirmassInjectionState::physicallyStopped() const {
#if EFI_SHAFT_POSITION_INPUT
	auto rpm = Sensor::get(SensorType::Rpm);
	return engine->rpmCalculator.getState() == STOPPED && engine->rpmCalculator.getCachedRpm() == 0 && rpm &&
		   rpm.Value == 0 && !engine->triggerCentral.engineMovedRecently();
#else
	return false;
#endif
}

void AirmassInjectionState::publishState() const {
	engine->outputChannels.blendedStatus = static_cast<uint8_t>(m_status);
	engine->outputChannels.blendedFault = static_cast<uint8_t>(m_fault);
}

void AirmassInjectionState::failCalculation(AirmassInjectionFault fault) {
	invalidateAirmassLoads();
	m_fault = fault;
	m_status = AirmassInjectionStatus::Faulted;
	m_calculationAccepted = false;
	m_standaloneReady = false;
	++m_epoch;
	publishState();
}

void AirmassInjectionState::observeMode(engine_load_mode_e mode) {
	if (!m_observedMode || m_mode != mode) {
		if (m_observedMode) {
			invalidateAirmassLoads();
			++m_epoch;
		}
		m_mode = mode;
		m_observedMode = true;
		m_calculationAccepted = false;
		m_standaloneReady = false;
		m_calculationFallback = AirmassInjectionFault::None;
		m_fault = AirmassInjectionFault::None;
		m_status = isComposite(mode) ? AirmassInjectionStatus::NotReady : AirmassInjectionStatus::Legacy;
	}
	publishState();
}

AirmassInjectionState::CalculationToken
AirmassInjectionState::beginCalculation(engine_load_mode_e mode, float rpm, int configurationVersion) {
	chibios_rt::CriticalSectionLocker csl;
	observeMode(mode);
	++m_epoch;
	m_positiveRpmCalculation = std::isfinite(rpm) && rpm > 0;
	// Keep the last completed publication available while its replacement is
	// calculated. Clearing admission on every fast callback can phase-align
	// with injection teeth and suppress every pulse at a constant engine speed.
	// Stops, mode/tune changes, and actual failures still close it immediately.
	const bool invalidatesPublication = !m_positiveRpmCalculation || m_configurationVersion != configurationVersion;
	if (invalidatesPublication) {
		m_standaloneReady = false;
	}
	m_configurationVersion = configurationVersion;
	m_calculationAccepted = false;
	m_calculationFallback = AirmassInjectionFault::None;
	if (invalidatesPublication &&
		(m_status == AirmassInjectionStatus::Ready || m_status == AirmassInjectionStatus::Degraded)) {
		m_status = AirmassInjectionStatus::NotReady;
	}
	publishState();
	return m_epoch;
}

void AirmassInjectionState::acceptCalculation(AirmassInjectionFault fallback) {
	chibios_rt::CriticalSectionLocker csl;
	m_calculationAccepted = true;
	m_calculationFallback = fallback;
	if (fallback != AirmassInjectionFault::None) {
		// A recovered calculation can fuel the engine, but must not train a VE map.
		engine->engineState.veAnalyzeSessionInvalid = true;
		engine->engineState.veAnalyzeEndpoint = 0;
		engine->outputChannels.blendedVeAnalyzeEndpoint = 0;
	}
}

void AirmassInjectionState::rejectCalculation(AirmassInjectionFault fault) {
	chibios_rt::CriticalSectionLocker csl;
	observeMode(engineConfiguration->fuelAlgorithm);
	if ((isComposite(m_mode) || needsStandalonePublication(m_mode)) && m_positiveRpmCalculation) {
		failCalculation(fault);
	} else if (needsStandalonePublication(m_mode)) {
		m_standaloneReady = false;
		m_calculationAccepted = false;
		++m_epoch;
		invalidateAirmassLoads();
	}
}

bool AirmassInjectionState::isCalculationCurrent(CalculationToken token) {
	chibios_rt::CriticalSectionLocker csl;
	observeMode(engineConfiguration->fuelAlgorithm);
	return token == m_epoch && m_configurationVersion == engine->getGlobalConfigurationVersion();
}

AirmassInjectionState::CalculationToken AirmassInjectionState::publicationEpoch() const {
	chibios_rt::CriticalSectionLocker csl;
	return m_epoch;
}

void AirmassInjectionState::completeCalculation(CalculationToken token, bool publicationValid) {
	chibios_rt::CriticalSectionLocker csl;
	observeMode(engineConfiguration->fuelAlgorithm);
	// A stale completion cannot close or reopen a newer publication.
	if (token != m_epoch || m_configurationVersion != engine->getGlobalConfigurationVersion()) {
		return;
	}
	if (!isComposite(m_mode) && !needsStandalonePublication(m_mode)) {
		return;
	}
	auto rpm = Sensor::get(SensorType::Rpm);
	if (!m_positiveRpmCalculation || !rpm || !std::isfinite(rpm.Value) || rpm.Value <= 0) {
		m_standaloneReady = false;
		m_status = AirmassInjectionStatus::NotReady;
		publishState();
		return;
	}
	if (!publicationValid || (needsStandalonePublication(m_mode) && !engine->engineState.airmassCalculationValid)) {
		failCalculation(AirmassInjectionFault::Result);
		return;
	}
	if (isComposite(m_mode) && !m_calculationAccepted) {
		m_status = AirmassInjectionStatus::NotReady;
		publishState();
		return;
	}
	m_standaloneReady = needsStandalonePublication(m_mode);
	m_fault = m_calculationFallback;
	m_status = m_fault != AirmassInjectionFault::None ? AirmassInjectionStatus::Degraded
			 : isComposite(m_mode)					  ? AirmassInjectionStatus::Ready
													  : AirmassInjectionStatus::Legacy;
	publishState();
}

void AirmassInjectionState::onEngineStop() {
	chibios_rt::CriticalSectionLocker csl;
	invalidateAirmassLoads(true);
	++m_epoch;
	m_calculationAccepted = false;
	m_positiveRpmCalculation = false;
	m_standaloneReady = false;
	m_status = isComposite(m_mode) ? AirmassInjectionStatus::NotReady : AirmassInjectionStatus::Legacy;
	m_fault = AirmassInjectionFault::None;
	publishState();
}

void AirmassInjectionState::onConfigurationWrite(engine_load_mode_e proposedMode, bool strategyChanged) {
	chibios_rt::CriticalSectionLocker csl;
	// A tune prepared while physically stopped can qualify on the next start.
	// A zero RPM sample while still moving cannot clear session invalidation.
	invalidateAirmassLoads(physicallyStopped());
	observeMode(engineConfiguration->fuelAlgorithm);
	++m_epoch;
	m_calculationAccepted = false;
	m_standaloneReady = false;
	if (strategyChanged) {
		observeMode(proposedMode);
	}
	m_status = isComposite(m_mode) ? AirmassInjectionStatus::NotReady : AirmassInjectionStatus::Legacy;
	m_fault = AirmassInjectionFault::None;
	publishState();
}

bool AirmassInjectionState::allowInjection() {
	chibios_rt::CriticalSectionLocker csl;
	observeMode(engineConfiguration->fuelAlgorithm);
	if (needsStandalonePublication(m_mode)) {
		// airmassCalculationValid describes the in-progress calculation. Only
		// completeCalculation may admit its final per-cylinder publication.
		return m_standaloneReady && m_configurationVersion == engine->getGlobalConfigurationVersion();
	}
	return m_status == AirmassInjectionStatus::Legacy ||
		   ((m_status == AirmassInjectionStatus::Ready || m_status == AirmassInjectionStatus::Degraded) &&
			m_configurationVersion == engine->getGlobalConfigurationVersion());
}

bool AirmassInjectionState::allowPrime() {
	// Priming uses its own upstream CLT, key-cycle, RPM and flood-clear checks.
	// Sensor recovery never requests a new prime pulse.
	return true;
}

AirmassInjectionStatus AirmassInjectionState::status() const {
	chibios_rt::CriticalSectionLocker csl;
	return m_status;
}

AirmassInjectionFault AirmassInjectionState::fault() const {
	chibios_rt::CriticalSectionLocker csl;
	return m_fault;
}

uint16_t AirmassInjectionState::pendingCallbacks() const {
	chibios_rt::CriticalSectionLocker csl;
	return m_pendingCallbacks;
}

bool AirmassInjectionState::callbacksAccepted(size_t count) {
	if (count > static_cast<size_t>(UINT16_MAX - m_pendingCallbacks)) {
		return false;
	}
	m_pendingCallbacks += count;
	return true;
}

void AirmassInjectionState::callbacksRejected(size_t count) {
	m_pendingCallbacks -= count;
}

void AirmassInjectionState::callbackCompleted() {
	chibios_rt::CriticalSectionLocker csl;
	efiAssertVoid(ObdCode::CUSTOM_ERR_ASSERT, m_pendingCallbacks > 0, "untracked injection callback");
	--m_pendingCallbacks;
}

bool scheduleFuelCallbacks(const ScheduledAction* events, size_t count, bool prime) {
	chibios_rt::CriticalSectionLocker csl;
	auto& state = engine->airmassInjectionState;
	if (!(prime ? state.allowPrime() : state.allowInjection())) {
		return false;
	}
	if (!isScheduleBatchValid(events, count, getTimeNowNt())) {
		state.rejectCalculation(AirmassInjectionFault::Scheduling);
		return false;
	}
	if (!state.callbacksAccepted(count)) {
		state.rejectCalculation(AirmassInjectionFault::Scheduling);
		return false;
	}
	if (!getScheduler()->scheduleBatch(events, count)) {
		state.callbacksRejected(count);
		state.rejectCalculation(AirmassInjectionFault::Scheduling);
		return false;
	}
	return true;
}
