/**
 * @file	idle_thread.h
 * @brief	Idle Valve Control thread
 *
 * @date May 23, 2013
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#pragma once

#include "engine_module.h"
#include "rusefi_types.h"
#include "efi_pid.h"
#include "sensor.h"
#include "idle_state_generated.h"
#include "biquad.h"


struct IIdleController {
	enum class Phase : uint8_t {
		Cranking,	// Below cranking threshold
		Idling,		// Below idle RPM, off throttle
		Coasting,	// Off throttle but above idle RPM
		CrankToIdleTaper, // Taper between cranking and idling
		Running,	// On throttle
	};

	struct TargetInfo {
		// Target speed for closed loop control
		float ClosedLoopTarget;

		// If below this speed, enter idle
		float IdleEntryRpm;

		// If above this speed, exit idle
		float IdleExitRpm;

		bool operator==(const TargetInfo& other) const {
			return ClosedLoopTarget == other.ClosedLoopTarget && IdleEntryRpm == other.IdleEntryRpm && IdleExitRpm == other.IdleExitRpm;
		}
	};

	virtual Phase determinePhase(float rpm, TargetInfo targetRpm, SensorResult tps, float vss, float crankingTaperFraction) = 0;
	virtual TargetInfo getTargetRpm(float clt) = 0;
	virtual float getCrankingOpenLoop(float clt) const = 0;
	virtual float getRunningOpenLoop(float rpm, float clt, SensorResult tps) = 0;
	virtual float getOpenLoop(Phase phase, float rpm, float clt, SensorResult tps, float crankingTaperFraction) = 0;
	virtual float getClosedLoop(Phase phase, float tps, float rpm, float target) = 0;
	virtual float getCrankingTaperFraction(float clt) const = 0;
	virtual bool isIdlingOrTaper() const = 0;
	virtual float getIdleTimingAdjustment(float rpm) = 0;
};

class IdleController : public IIdleController, public EngineModule, public idle_state_s {
public:
	// Mockable<> interface
	using interface_t = IIdleController;

	void init();

	float getIdlePosition(float rpm);

	// TARGET DETERMINATION
	TargetInfo getTargetRpm(float clt) override;

	// PHASE DETERMINATION: what is the driver trying to do right now?
	Phase determinePhase(float rpm, TargetInfo targetRpm, SensorResult tps, float vss, float crankingTaperFraction) override;
	float getCrankingTaperFraction(float clt) const override;

	// OPEN LOOP CORRECTIONS
	percent_t getCrankingOpenLoop(float clt) const override;
	percent_t getRunningOpenLoop(float rpm, float clt, SensorResult tps) override;
	percent_t getOpenLoop(Phase phase, float rpm, float clt, SensorResult tps, float crankingTaperFraction) override;

	float getIdleTimingAdjustment(float rpm) override;
	float getIdleTimingAdjustment(float rpm, float targetRpm, Phase phase);

	// CLOSED LOOP CORRECTION
	float getClosedLoop(IIdleController::Phase phase, float tpsPos, float rpm, float targetRpm) override;

	void onConfigurationChange(engine_configuration_s const * previousConfig) final;
	void onFastCallback() final;

	// Allow querying state from outside
	bool isIdlingOrTaper() const override {
		return m_lastPhase == Phase::Idling || (engineConfiguration->useSeparateIdleTablesForCrankingTaper && m_lastPhase == Phase::CrankToIdleTaper);
	}

private:
	Pid m_pid;

	// These are stored by getIdlePosition() and used by getIdleTimingAdjustment()
	Phase m_lastPhase = Phase::Cranking;
	int m_lastTargetRpm = 0;
	efitimeus_t restoreAfterPidResetTimeUs = 0;

	Timer m_timeInIdlePhase;

	Pid m_timingPid;

	float m_modeledFlowIdleTiming = 0;
	Biquad m_timingHpf;
};

percent_t getIdlePosition();

void applyIACposition(percent_t position);
void setManualIdleValvePosition(int positionPercent);

void startIdleThread();
void setDefaultIdleParameters();
void startIdleBench(void);
void setIdleMode(idle_mode_e value);
void setTargetIdleRpm(int value);
void startPedalPins();
void stopPedalPins();
