/*
 * @file torque_reduction_controller.cpp
 *
 * @see torque_reduction_controller.h
 */

#include "pch.h"

#include "torque_reduction_controller.h"
#include "torque_model.h"

void TorqueReductionController::setReductionRequest(float request) {
	m_reductionRequest = request;
}

TorqueReductionOutput TorqueReductionController::getReduction(float request) const {
	TorqueReductionOutput result;

	// A request outside [0, 1] is meaningless - clamp it.
	request = clampF(0, request, 1);

	auto& reqBins = config->torqueReductionRetardReqBins;

	// Spark retard comes straight from the calibrated table; interpolate2d holds it at the
	// last value once the request runs off the end of the table's axis.
	result.retardDeg = interpolate2d(request, reqBins, config->torqueReductionRetard);

	// Cut delivers whatever reduction spark retard can't: it begins where the request runs
	// off the end of the retard table's axis (retard now held at its maximum) and ramps to
	// a full cut at a request of 1. A table whose axis already reaches a request of 1 leaves
	// no room for cut, so it is retard-only.
	float cutStart = reqBins[efi::size(reqBins) - 1];
	if (request <= cutStart || cutStart >= 1) {
		result.cutFraction = 0;
	} else {
		result.cutFraction = clampF(0, (request - cutStart) / (1 - cutStart), 1);
	}

	return result;
}

angle_t TorqueReductionController::update() {
	float request = m_reductionRequest;
	auto out = getReduction(request);

	// Publish for logging
	reductionRequest = 100 * request; // logs in percent
	retardApplied = out.retardDeg;
	cutFraction = 100 * out.cutFraction; // logs in percent

#if EFI_LAUNCH_CONTROL
	engine->torqueReductionSparkLimiter.setTargetSkipRatio(out.cutFraction);
#endif // EFI_LAUNCH_CONTROL

	return out.retardDeg;
}
