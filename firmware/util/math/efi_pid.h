/**
 * @file efi_pid.h
 *
 * everyone including ChibiOS-Contrib has a version of 'pid.h' so we use unique file name to avoid drama
 *
 *
 * @date Sep 16, 2014
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#pragma once

#include "pid_state_generated.h"
#include "output_channels_generated.h"

struct pid_s;

/**
 * default basic implementation also known as PidParallelController
 */
class Pid final : public pid_state_s {
public:
	Pid();
	explicit Pid(pid_s *parameters);
	void initPidClass(pid_s *parameters);
	bool isSame(const pid_s *parameters) const;

	float getOutput(float target, float input, float dTime);

	void reset();

	float getIntegration() const;
	void setErrorAmplification(float coef);
#if EFI_TUNER_STUDIO
	void postState(pid_status_s& pidStatus) const;
#endif /* EFI_TUNER_STUDIO */
	int resetCounter;
	// todo: move this to pid_s one day
	float iTermMin = -1000000.0;
	float iTermMax =  1000000.0;
protected:
	pid_s *m_parameters = nullptr;
	void updateITerm(float value);

private:
	// doesn't limit the result
	float getUnclampedOutput(float target, float input, float dTime);
};
