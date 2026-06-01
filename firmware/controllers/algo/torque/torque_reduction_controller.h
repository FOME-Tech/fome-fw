/*
 * @file torque_reduction_controller.h
 *
 * Takes a torque-reduction request in [0, 1] (0 = deliver full torque, 1 =
 * eliminate torque) and turns it into ignition retard plus, once retard
 * saturates, cylinder cut. This is the reusable primitive that both spark-only
 * traction control and the full torque model layer on top of.
 */

#pragma once

#include "torque_reduction_state_generated.h"

struct TorqueReductionOutput {
	// Ignition retard to apply, degrees, >= 0.
	angle_t retardDeg = 0;
	// Cylinder cut fraction, [0, 1], for the spark limiter.
	float cutFraction = 0;
};

class TorqueReductionController : public torque_reduction_state_s {
public:
	// Producer entry point. request: 0 = deliver full torque, 1 = eliminate torque.
	void setReductionRequest(float request);

	// Pure mapping of a reduction request to retard + cut, driven by calibration.
	// Exposed (and side-effect free) for unit testing.
	TorqueReductionOutput getReduction(float request) const;

	// Called once per ignition pass: evaluates the current request, publishes logged
	// state, arms the cut limiter, and returns the spark retard to apply.
	angle_t update();

private:
	float m_reductionRequest = 0;
};
