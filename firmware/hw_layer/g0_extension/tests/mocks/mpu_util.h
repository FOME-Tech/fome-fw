#pragma once
struct hardware_pwm {
	virtual void setDuty(float duty) = 0;
	virtual void setFrequency(float) {}
};
