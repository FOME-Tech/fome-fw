#pragma once

#include "rusefi_types.h"
#include "scheduler.h"

namespace chibios_rt {
class CriticalSectionLocker;
}

// These values are also exposed in the composite diagnostics.
enum class AirmassInjectionStatus : uint8_t {
	Legacy,
	NotReady,
	Ready,
	Faulted,
	Degraded
};
enum class AirmassInjectionFault : uint8_t {
	None,
	Configuration,
	Sensor,
	Correction,
	Result,
	Load,
	StrategyChange,
	Scheduling
};

// Owns only injection admission/recovery. Accepted output callbacks always run to completion.
class AirmassInjectionState {
public:
	using CalculationToken = uint32_t;

	CalculationToken beginCalculation(engine_load_mode_e mode, float rpm, int configurationVersion);
	void acceptCalculation(AirmassInjectionFault fallback = AirmassInjectionFault::None);
	void rejectCalculation(AirmassInjectionFault fault);
	void completeCalculation(CalculationToken token, bool publicationValid);
	bool isCalculationCurrent(CalculationToken token);
	// Read-only capture for model evaluation; does not observe or change modes.
	CalculationToken publicationEpoch() const;
	void onEngineStop();
	void onConfigurationWrite(engine_load_mode_e proposedMode, bool strategyChanged);

	bool allowInjection();
	bool allowPrime();
	AirmassInjectionStatus status() const;
	AirmassInjectionFault fault() const;
	uint16_t pendingCallbacks() const;

	// Access only within the lifetime/scope of the supplied critical section.
	// This avoids nested save/restore in already guarded publications and admission.
	class LockedAccess {
	public:
		LockedAccess(const LockedAccess&) = delete;
		LockedAccess& operator=(const LockedAccess&) = delete;
		bool allowInjection();
		bool isCalculationCurrent(CalculationToken token);
		CalculationToken publicationEpoch() const;
		void completeCalculation(CalculationToken token, bool publicationValid);
		void rejectCalculation(AirmassInjectionFault fault);
		bool callbacksAccepted(size_t count);
		void callbacksRejected(size_t count);
		void callbackCompleted();

	private:
		friend class AirmassInjectionState;
		LockedAccess(AirmassInjectionState& state, chibios_rt::CriticalSectionLocker&)
			: m_state(state) {}
		AirmassInjectionState& m_state;
	};
	LockedAccess locked(chibios_rt::CriticalSectionLocker& lock) {
		return LockedAccess(*this, lock);
	}
	void callbackCompleted();

private:
	void observeMode(engine_load_mode_e mode);
	void failCalculation(AirmassInjectionFault fault);
	void setStatus(AirmassInjectionStatus status, AirmassInjectionFault fault);
	bool allowInjectionLocked();
	bool isCalculationCurrentLocked(CalculationToken token);
	void completeCalculationLocked(CalculationToken token, bool publicationValid);
	void rejectCalculationLocked(AirmassInjectionFault fault);
	bool callbacksAccepted(size_t count);
	void callbacksRejected(size_t count);
	void callbackCompletedLocked();
	bool physicallyStopped() const;

	uint32_t m_epoch = 0;
	int m_configurationVersion = 0;
	engine_load_mode_e m_mode = static_cast<engine_load_mode_e>(0);
	uint16_t m_pendingCallbacks = 0;
	AirmassInjectionStatus m_status = AirmassInjectionStatus::Legacy;
	AirmassInjectionFault m_fault = AirmassInjectionFault::None;
	bool m_observedMode = false;
	bool m_calculationAccepted = false;
	AirmassInjectionFault m_calculationFallback = AirmassInjectionFault::None;
	bool m_positiveRpmCalculation = false;
	// Standalone physical models require their own completed fuel publication;
	// priming retains its independent startup admission.
	bool m_standaloneReady = false;
};

// All events must use callbacks which acknowledge completion exactly once.
// Admission/count registration and executor insertion share the same critical section.
bool scheduleFuelCallbacks(const ScheduledAction* events, size_t count, bool prime = false);
