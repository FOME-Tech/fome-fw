/**
 * @file pid.cpp
 *
 * https://en.wikipedia.org/wiki/Feedback
 * http://en.wikipedia.org/wiki/PID_controller
 *
 * @date Sep 16, 2014
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"

#include "efi_pid.h"
#include "math.h"

Pid::Pid() : Pid(nullptr) { }

Pid::Pid(pid_s *parameters) {
	initPidClass(parameters);
}

void Pid::initPidClass(pid_s *parameters) {
	m_parameters = parameters;
	resetCounter = 0;

	reset();
}

bool Pid::isSame(const pid_s *parameters) const {
	if (!m_parameters) {
		// this 'null' could happen on first execution during initialization
		return false;
	}

	efiAssert(ObdCode::OBD_PCM_Processor_Fault, parameters != NULL, "PID::isSame NULL", false);
	return m_parameters->pFactor == parameters->pFactor
			&& m_parameters->iFactor == parameters->iFactor
			&& m_parameters->dFactor == parameters->dFactor
			&& m_parameters->offset == parameters->offset;
}

float Pid::getUnclampedOutput(float target, float input, float dTime) {
	float error = (target - input) * errorAmplificationCoef;
	lastTarget = target;
	lastInput = input;

	float pTerm = m_parameters->pFactor * error;
	updateITerm(m_parameters->iFactor * dTime * error);
	dTerm = m_parameters->dFactor / dTime * (error - previousError);

	previousError = error;

	if (dTime <=0) {
		warning(ObdCode::CUSTOM_PID_DTERM, "PID: unexpected dTime");
		return pTerm + m_parameters->offset;
	}

	return pTerm + iTerm + dTerm + m_parameters->offset;
}

float Pid::getOutput(float target, float input, float dTime) {
	float output = getUnclampedOutput(target, input, dTime);

	if (output > m_parameters->maxValue) {
		output = m_parameters->maxValue;
	} else if (output < m_parameters->minValue) {
		output = m_parameters->minValue;
	}

	lastOutput = output;

	return output;
}

void Pid::reset() {
	dTerm = iTerm = 0;
	lastOutput = lastInput = lastTarget = previousError = 0;
	errorAmplificationCoef = 1.0f;
	resetCounter++;
}

float Pid::getIntegration() const {
	return iTerm;
}

void Pid::setErrorAmplification(float coef) {
	errorAmplificationCoef = coef;
}

#if EFI_TUNER_STUDIO
void Pid::postState(pid_status_s& pidStatus) const {
	pidStatus.output = lastOutput;
	pidStatus.error = previousError;
	pidStatus.pTerm = m_parameters == nullptr ? 0 : m_parameters->pFactor * previousError;
	pidStatus.iTerm = iTerm;
	pidStatus.dTerm = dTerm;
}
#endif /* EFI_TUNER_STUDIO */

void Pid::updateITerm(float value) {
	iTerm += value;
	/**
	 * If we have exceeded the ability of the controlled device to hit target, the I factor will keep accumulating and approach infinity.
	 * Here we limit the I-term #353
	 */
	if (iTerm > m_parameters->maxValue * 100) {
		iTerm = m_parameters->maxValue * 100;
	}
	if (iTerm > iTermMax) {
		iTerm = iTermMax;
	}

	// this is kind of a hack. a proper fix would be having separate additional settings 'maxIValue' and 'minIValye'
	if (iTerm < -m_parameters->maxValue * 100)
		iTerm = -m_parameters->maxValue * 100;
	if (iTerm < iTermMin) {
		iTerm = iTermMin;
	}
}
