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

Pid::Pid() {
	initPidClass(nullptr);
}

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
			&& m_parameters->offset == parameters->offset
			&& m_parameters->periodMs == parameters->periodMs;
}

/**
 * @param Controller input / process output
 * @returns Output from the PID controller / the input to the process
 */
float Pid::getOutput(float target, float input) {
	float dTime = MS2SEC(GET_PERIOD_LIMITED(m_parameters));
	return getOutput(target, input, dTime);
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
		return pTerm + getOffset();
	}

	return pTerm + iTerm + dTerm + getOffset();
}

/**
 * @param dTime seconds probably? :)
 */
float Pid::getOutput(float target, float input, float dTime) {
	float output = getUnclampedOutput(target, input, dTime);

	if (output > m_parameters->maxValue) {
		output = m_parameters->maxValue;
	} else if (output < getMinValue()) {
		output = getMinValue();
	}

	lastOutput = output;

	return output;
}

void Pid::updateFactors(float pFactor, float iFactor, float dFactor) {
	m_parameters->pFactor = pFactor;
	m_parameters->iFactor = iFactor;
	m_parameters->dFactor = dFactor;
	reset();
}

void Pid::reset() {
	dTerm = iTerm = 0;
	lastOutput = lastInput = lastTarget = previousError = 0;
	errorAmplificationCoef = 1.0f;
	resetCounter++;
}

float Pid::getP() const {
	return m_parameters->pFactor;
}

float Pid::getI() const {
	return m_parameters->iFactor;
}

float Pid::getPrevError() const {
	return previousError;
}

float Pid::getIntegration() const {
	return iTerm;
}

float Pid::getD() const {
	return m_parameters->dFactor;
}

float Pid::getOffset(void) const {
	return m_parameters->offset;
}

float Pid::getMinValue(void) const {
	return m_parameters->minValue;
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

void Pid::sleep() {
#if !EFI_UNIT_TEST
	int periodMs = maxI(10, m_parameters->periodMs);
	chThdSleepMilliseconds(periodMs);
#endif /* EFI_UNIT_TEST */
}

void Pid::showPidStatus(const char*msg) const {
	efiPrintf("%s settings: offset=%f P=%.5f I=%.5f D=%.5f period=%dms",
			msg,
			getOffset(),
			m_parameters->pFactor,
			m_parameters->iFactor,
			m_parameters->dFactor,
			m_parameters->periodMs);

	efiPrintf("%s status: value=%.2f input=%.2f/target=%.2f iTerm=%.5f dTerm=%.5f",
			msg,
			lastOutput,
			lastInput,
			lastTarget,
			iTerm, dTerm);

}

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
