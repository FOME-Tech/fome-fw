/*
 * boost_control.h
 *
 *  Created on: 18. aug. 2019
 *      Author: Ola Ruud
 */
#pragma once

#include "closed_loop_controller.h"
#include "efi_pid.h"
#include "boost_control_generated.h"

struct IPwm;

class BoostController : public EngineModule, public boost_control_s, public ClosedLoopController<float, percent_t> {
public:
	void
	init(IPwm* pmw, const ValueProvider3D* openLoopMap, const ValueProvider3D* closedLoopTargetMap, pid_s* pidParams);

	void onFastCallback() override;
	void resetLua();

	bool hasInit() const {
		return m_hasInitBoost;
	}

	// Called when the configuration may have changed.  Controller will
	// reset if necessary.
	void onConfigurationChange(engine_configuration_s const* previousConfig) override;

	// Helpers for individual parts of boost control
	expected<float> observePlant() const override;
	expected<float> getSetpoint() override;

	expected<percent_t> getOpenLoop(float target) override;
	expected<percent_t> getClosedLoop(float target, float manifoldPressure) override;

	void setOutput(expected<percent_t> outputValue) override;

	// Most recently computed closed loop target. Kept current even while the
	// RPM/TPS/MAP thresholds hold the controller at its safe duty.
	expected<float> getLastTarget() const {
		return m_lastTarget;
	}

private:
	expected<float> getSetpointImpl();

	expected<float> m_lastTarget = unexpected;

	bool m_hasInitBoost = false;
	bool m_shouldResetPid = false;

	percent_t getClosedLoopImpl(float target, float manifoldPressure);

	Pid m_pid;

	const ValueProvider3D* m_openLoopMap = nullptr;
	const ValueProvider3D* m_closedLoopTargetMap = nullptr;
	IPwm* m_pwm = nullptr;
};

// Configuration sequence step, see config_steps.cpp: the controller and its output pin
void stopBoostControl(const engine_configuration_s* prev);
void startBoostControl(const engine_configuration_s* prev);
void setDefaultBoostParameters();
