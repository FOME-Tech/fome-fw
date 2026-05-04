#pragma once

#include "fan_control_generated.h"

struct IPwm;

struct FanController : public EngineModule, public fan_control_s {
	void onSlowCallback() override;

	void onIgnitionStateChanged(bool ignitionOn) override;

	void benchTest();

private:
	bool getState(bool acActive, bool lastState);
	float getPwmDuty(bool acActive);

	bool m_ignitionState = false;
	Timer m_benchTestTimer;
	IPwm* m_pwm = nullptr;

public:
	// For unit testing only
	void setMockPwm(IPwm* pwm) {
		m_pwm = pwm;
	}

protected:
	virtual OutputPin& getPin() = 0;
	virtual float getFanOnTemp() = 0;
	virtual float getFanOffTemp() = 0;
	virtual bool enableWithAc() = 0;
	virtual bool disableWhenStopped() = 0;

	// PWM mode methods
	virtual bool usePwmMode() = 0;
	virtual float getAcOffDuty(float clt) = 0;
	virtual float getAcOnDuty(float clt) = 0;

	friend void initFanControl();
};

struct FanControl1 : public FanController {
	OutputPin& getPin() override {
		return enginePins.fanRelay;
	}

	float getFanOnTemp() override {
		return engineConfiguration->fanOnTemperature;
	}

	float getFanOffTemp() override {
		return engineConfiguration->fanOffTemperature;
	}

	bool enableWithAc() override {
		return engineConfiguration->enableFan1WithAc;
	}

	bool disableWhenStopped() override {
		return engineConfiguration->disableFan1WhenStopped;
	}

	// PWM mode methods
	bool usePwmMode() override {
		return engineConfiguration->fan1UsePwmMode;
	}

	float getAcOffDuty(float clt) override;
	float getAcOnDuty(float clt) override;
};

struct FanControl2 : public FanController {
	OutputPin& getPin() override {
		return enginePins.fanRelay2;
	}

	float getFanOnTemp() override {
		return engineConfiguration->fan2OnTemperature;
	}

	float getFanOffTemp() override {
		return engineConfiguration->fan2OffTemperature;
	}

	bool enableWithAc() override {
		return engineConfiguration->enableFan2WithAc;
	}

	bool disableWhenStopped() override {
		return engineConfiguration->disableFan2WhenStopped;
	}

	// PWM mode methods
	bool usePwmMode() override {
		return engineConfiguration->fan2UsePwmMode;
	}

	float getAcOffDuty(float clt) override;
	float getAcOnDuty(float clt) override;
};

void initFanControl();
