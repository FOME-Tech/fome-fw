/*
 * @file traction_control.h
 *
 * Closed-loop traction control. Measures wheel slip (driven axle vs the undriven
 * reference axle) and publishes a brake-torque ceiling that plugs into the torque
 * model as a limit producer (see TorqueModel::applyTorqueLimits). The controller
 * works in the axle domain - it computes an absolute permitted axle torque from a
 * PID on slip error, clamped to the live torque request, then converts back to an
 * engine-torque ceiling via the current gear ratio. Requires gear detection.
 *
 * Full design rationale: traction_control.md at the repository root.
 */

#pragma once

#include "traction_control_state_generated.h"
#include "efi_pid.h"

class TractionController : public traction_control_state_s {
public:
	// Slip measurement, broken out so it can be tested in isolation. unexpected means
	// "disarmed": no valid reference axle, or below the minimum speed.
	struct SlipInfo {
		float slipPercent;
		float drivenSpeed;
		float referenceSpeed;
	};
	expected<SlipInfo> getSlip() const;

	// 2D target-slip lookup (reference speed Y, trim channel X) clamped to slipTargetMax.
	// Side-effect free for testing.
	float getTargetSlip(float refSpeed);

	// Producer entry point, called every fast tick from TorqueModel::applyTorqueLimits.
	// Returns the engine-domain torque ceiling, or unexpected when disarmed (no limit).
	expected<float> getTorqueLimit(float torqueRequested);

private:
	// Park the controller in its released state and report "no limit".
	expected<float> disarm();

	Pid m_slipPid;

	// Previous rate-limited cut depth: axle torque held below the demand rail. The asymmetric rate
	// limit acts on this gap (not the absolute ceiling), so a moving rail tracks instantly and the
	// limit only ever shapes the slip-driven cut.
	float m_prevCut = 0;
	// Previous slip, for the slip-rate (D-term) estimate.
	float m_prevSlip = 0;
	// Whether TC was armed last tick, to seed state cleanly on the arm transition.
	bool m_armed = false;
	// Whether last cycle's output sat at the rail (TC not limiting). While released the integrator is
	// pinned to the rail so the ceiling tracks the pedal with no lag (clamping anti-windup).
	bool m_released = true;
};
