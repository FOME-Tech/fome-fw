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
#include "idle_target_generated.h"
#include "biquad.h"

struct IIdleController {
	enum class Phase : uint8_t {
		Cranking,		  // Below cranking threshold
		Idling,			  // Below idle RPM, off throttle
		Coasting,		  // Off throttle but above idle RPM
		CrankToIdleTaper, // Taper between cranking and idling
		Running,		  // On throttle
	};

	struct TargetInfo {
		// Target speed for closed loop control
		float ClosedLoopTarget;

		// If below this speed, enter idle
		float IdleEntryRpm;

		// If above this speed, exit idle
		float IdleExitRpm;

		bool operator==(const TargetInfo& other) const {
			return ClosedLoopTarget == other.ClosedLoopTarget && IdleEntryRpm == other.IdleEntryRpm &&
				   IdleExitRpm == other.IdleExitRpm;
		}
	};

	virtual float getCrankingOpenLoop(float clt) const = 0;
	virtual float getRunningOpenLoop(float clt) = 0;
	virtual float getOpenLoop(Phase phase, float rpm, float clt, float crankingTaperFraction) = 0;
	virtual float getClosedLoop(Phase phase, float rpm, float rpmRate, float target) = 0;
	virtual bool isIdlingOrTaper() const = 0;
	virtual float getIdleTimingAdjustment(float rpm, float rpmRate) = 0;
};

class IdleController : public IIdleController, public EngineModule, public idle_state_s {
public:
	// Mockable<> interface
	using interface_t = IIdleController;

	void init();

	float getIdlePosition(float rpm, float rpmRate);

	// OPEN LOOP CORRECTIONS
	percent_t getCrankingOpenLoop(float clt) const override;
	percent_t getRunningOpenLoop(float clt) override;
	percent_t getOpenLoop(Phase phase, float rpm, float clt, float crankingTaperFraction) override;

	// Extra air carried over from before idle entry, ramped out on return to idle
	percent_t getReturnToIdleAir(Phase phase, percent_t idleOpenLoop, float returnRampFraction);

	float getIdleTimingAdjustment(float rpm, float rpmRate) override;
	float getIdleTimingAdjustment(float rpm, float rpmRate, float targetRpm, Phase phase);

	// CLOSED LOOP CORRECTION
	float getClosedLoop(IIdleController::Phase phase, float rpm, float rpmRate, float targetRpm) override;

	void onConfigurationChange(engine_configuration_s const* previousConfig) override final;
	void onFastCallback() override final;
	void onEngineStop() override final;

	// Allow querying state from outside
	bool isIdlingOrTaper() const override {
		return m_lastPhase == Phase::Idling ||
			   (engineConfiguration->useSeparateIdleTablesForCrankingTaper && m_lastPhase == Phase::CrankToIdleTaper);
	}

#ifdef EFI_UNIT_TEST
	Pid* getIdlePid() {
		return &m_pid;
	}
#endif

private:
	Pid m_pid;

	// These are stored by getIdlePosition() and used by getIdleTimingAdjustment()
	Phase m_lastPhase = Phase::Cranking;
	int m_lastTargetRpm = 0;

	// Open loop position commanded on the last tick, and the extra air latched at idle entry
	percent_t m_lastOpenLoop = 0;
	percent_t m_returnAirStart = 0;
	efitimeus_t restoreAfterPidResetTimeUs = 0;

	Pid m_timingPid;
};

struct IIdleTargetController {
	using Phase = IIdleController::Phase;
	using TargetInfo = IIdleController::TargetInfo;

	struct Output {
		TargetInfo target;
		Phase phase = Phase::Cranking;
		float crankingTaperFraction = 0;

		// How much of the return to idle ramp is left: 1 = just entered idle from the entry threshold,
		// 0 = ramp finished or disabled. Already scaled down if the engine entered idle near target.
		float returnRampFraction = 0;
	};

	virtual TargetInfo getTargetRpm(float clt) = 0;
	virtual Phase determinePhase(
			float rpm, TargetInfo targetRpm, bool tpsIsAboveIdleThreshold, float vss, float crankingTaperFraction) = 0;
	virtual float getCrankingTaperFraction(float clt) const = 0;

	virtual Output getOutput(bool tpsIsAboveThreshold) = 0;
};

class IdleTargetController : public IIdleTargetController, public EngineModule, public idle_target_s {
public:
	// Mockable<> interface
	using interface_t = IIdleTargetController;

	TargetInfo getTargetRpm(float clt) override;
	Phase determinePhase(
			float rpm,
			TargetInfo targetRpm,
			bool tpsIsAboveIdleThreshold,
			float vss,
			float crankingTaperFraction) override;
	float getCrankingTaperFraction(float clt) const override;

	Output getOutput(bool tpsIsAboveIdleThreshold) override;

	float getReturnRampFraction() const;

private:
	Phase m_lastPhase = Phase::Cranking;

	Timer m_timeSinceCranking;
	Timer m_timeInIdlePhase;

	// Where the return to idle ramp starts, as a fraction of the way from the normal target to the
	// entry threshold, latched at idle entry
	float m_rampStartFraction = 0;
};

percent_t getIdlePosition();

void applyIACposition(percent_t position);
void setManualIdleValvePosition(int positionPercent);

void startIdleThread();
void setDefaultIdleParameters();
void startIdleBench();
void setIdleMode(idle_mode_e value);
void setTargetIdleRpm(int value);
void startPedalPins();
void stopPedalPins();
