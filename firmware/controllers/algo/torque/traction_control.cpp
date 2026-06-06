/*
 * @file traction_control.cpp
 *
 * @see traction_control.h
 */

#include "pch.h"

#include "traction_control.h"
#include "gppwm_channel.h"

#include <algorithm>

// Combine two wheel-speed sensors into one axle speed, degrading to whichever single wheel is valid
// (one dead sensor). unexpected only when neither wheel reads. useFastest takes the faster wheel
// instead of the mean: on an open diff a peeling wheel runs away in speed while the planted one
// stays put, so the mean halves the slip and hides the event - max() catches it. Wheel speed is a
// velocity (additive), so the mean is the arithmetic mean; a geometric/harmonic mean would bias
// toward the slower wheel, the wrong direction for peel detection.
static expected<float> combinePair(SensorType a, SensorType b, bool useFastest) {
	auto va = Sensor::get(a);
	auto vb = Sensor::get(b);

	if (va && vb) {
		return useFastest ? std::max(va.Value, vb.Value) : (va.Value + vb.Value) / 2;
	}
	if (va) {
		return va.Value;
	}
	if (vb) {
		return vb.Value;
	}
	return unexpected;
}

expected<TractionController::SlipInfo> TractionController::getSlip() const {
	// drivenAxleIsFront: true = Front, false = False. The opposite (undriven) axle is the slip reference.
	bool frontDriven = engineConfiguration->tractionControl.drivenAxleIsFront;

	SensorType drivenL = frontDriven ? SensorType::WheelSpeedLF : SensorType::WheelSpeedLR;
	SensorType drivenR = frontDriven ? SensorType::WheelSpeedRF : SensorType::WheelSpeedRR;
	SensorType refL = frontDriven ? SensorType::WheelSpeedLR : SensorType::WheelSpeedLF;
	SensorType refR = frontDriven ? SensorType::WheelSpeedRR : SensorType::WheelSpeedRF;

	// Driven axle honours the combine mode (Fastest for an open diff); the reference axle is free-
	// rolling with both wheels near-equal, so it always uses the mean (vehicle-centerline speed).
	auto driven = combinePair(drivenL, drivenR, engineConfiguration->tractionControl.drivenSlipUseFastest);
	auto reference = combinePair(refL, refR, false);

	// No ground truth, or too slow to define a meaningful slip ratio: disarm.
	if (!driven || !reference) {
		return unexpected;
	}
	if (reference.Value <= 0 || reference.Value < engineConfiguration->tractionControl.minimumSpeed) {
		return unexpected;
	}

	float slip = (driven.Value - reference.Value) / reference.Value * 100;
	return SlipInfo{slip, driven.Value, reference.Value};
}

float TractionController::getTargetSlip(float refSpeed) {
	// X axis is an optional trim source (e.g. a driver "TC dial"); unset reads 0 -> first column.
	float trim = readGppwmChannel(engineConfiguration->tractionControl.slipTargetYAxis).value_or(0);
	slipTargetYAxisValue = trim;

	// Table is indexed [speed][trim]: speed is the row (Y) axis, trim the column (X).
	float target = interpolate3d(
			config->slipTargetTable, config->slipTargetSpeedBins, refSpeed, config->slipTargetTrimBins, trim);

	return clampF(0, target, engineConfiguration->tractionControl.slipTargetMax);
}

expected<float> TractionController::disarm() {
	m_armed = false;
	axleTorqueLimit = 0;
	engineTorqueLimit = 0;
	return unexpected;
}

expected<float> TractionController::getTorqueLimit(float torqueRequested) {
	auto& cfg = engineConfiguration->tractionControl;

	if (!engineConfiguration->enableTractionControl) {
		return disarm();
	}

	// Slip needs a valid reference axle; the axle<->engine conversion needs a known gear ratio.
	// Either missing means there's nothing safe to publish - disarm.
	auto slip = getSlip();
	auto ratio = engine->module<GearDetector>()->getTotalRatioInCurrentGear();
	if (!slip || !ratio) {
		return disarm();
	}

	slipMeasured = slip.Value.slipPercent;
	drivenSpeed = slip.Value.drivenSpeed;
	referenceSpeed = slip.Value.referenceSpeed;

	const float dt = FAST_CALLBACK_PERIOD_MS / 1000.0f;

	// The whole controller lives in the axle domain (gear-independent), so convert the engine-domain request to
	// axle-domain so we can use it as the upper limit for the traction control output
	float axleDemand = torqueRequested * ratio.Value;

	// Target a slip *ratio* (peak grip sits at a roughly constant ratio, independent of speed) but
	// control on slip *speed* in kph. The plant gain from axle torque to slip speed is r / I_eff -
	// independent of vehicle speed - whereas the gain to slip ratio carries a 1/v from the percent
	// normalization. Converting the ratio target to a speed setpoint moves that 1/v into the
	// setpoint, where it cancels exactly, leaving the loop gain speed-independent. So the PID gains
	// are axle-Nm per kph and need no speed scheduling.
	float targetPct = getTargetSlip(slip.Value.referenceSpeed);
	slipTarget = targetPct;

	float slipSpeed = slip.Value.drivenSpeed - slip.Value.referenceSpeed; // kph
	float targetSpeed = targetPct * slip.Value.referenceSpeed * 0.01f;	  // kph

	if (!m_slipPid.isSame(&cfg.slipPid)) {
		m_slipPid.initPidClass(&cfg.slipPid);
		m_armed = false;
	}

	// On the arm transition (disarmed last tick) start released and tracking the rail, with no
	// carried-over cut and no bogus first-tick slip-rate spike.
	if (!m_armed) {
		m_released = true;
		m_prevCut = 0;
		m_prevSlipSpeed = slipSpeed;
		m_armed = true;
	}

	float slipRateValue = (slipSpeed - m_prevSlipSpeed) / dt; // kph/s
	m_prevSlipSpeed = slipSpeed;
	slipRate = slipRateValue;

	// Dynamic anti-windup: the integrator can't wind above the live axle demand (idle-PID pattern).
	// When the request falls - or an upshift drops the ratio - iTermMax falls with it.
	m_slipPid.iTermMax = axleDemand;
	m_slipPid.iTermMin = 0;
	// error = targetSpeed - slipSpeed. Override D with -slipRate to anticipate fast onset without
	// derivative kick from quantized wheel speed.
	m_slipPid.setDTermOverride(-slipRateValue);

	// Conditional integration. While TC isn't actually limiting (last cycle's output sat at the
	// rail), pin the integrator to the rail so the released ceiling tracks the pedal with zero lag:
	// a fast tip-in is followed immediately instead of being clipped while the integral catches up.
	// P and D can still pull the output below the rail this same tick to start a bite; once it's
	// limiting we stop pinning and let the integrator hold the cut. This is clamping anti-windup
	// against the driver-demand rail.
	if (m_released) {
		m_slipPid.iTerm = axleDemand;
	}

	float rawAxle = m_slipPid.getOutput(targetSpeed, slipSpeed, dt);
	float rawCeiling = clampF(0, rawAxle, axleDemand);

	// Rate-limit the cut depth (gap below the rail), not the absolute ceiling. Expressed relative to
	// the rail, a moving rail (tip-in / upshift) produces no artificial motion, so the limit only
	// ever shapes the slip-driven cut: grow fast (bite, engageRate default unlimited), shrink slowly
	// (handback, releaseRate) so a wheel hooking up doesn't dump torque back and re-break traction.
	float rawCut = axleDemand - rawCeiling;
	float cut = limitRateOfChange(rawCut, m_prevCut, cfg.engageRate, cfg.releaseRate, dt);
	m_prevCut = cut;

	float axleCeiling = axleDemand - cut;
	// Released next tick iff we're back at the rail (no cut), i.e. TC isn't limiting.
	m_released = cut <= 0;
	axleTorqueLimit = axleCeiling;

	// Back to the engine domain for arbitration, with the stall floor applied as an engine-domain
	// clamp (a minimum-RPM concern, not an axle one).
	float engineCeiling = std::max(axleCeiling / ratio.Value, (float)cfg.engineTorqueFloor);
	engineTorqueLimit = engineCeiling;

	return engineCeiling;
}
