#pragma once

#include "fan_control_generated.h"

struct FanController : public EngineModule, public fan_control_s {
	void onSlowCallback() override;

	void onIgnitionStateChanged(bool ignitionOn) override;

	void benchTest();

private:
	bool getState(bool acActive, bool lastState);

	bool m_ignitionState = false;
	Timer m_benchTestTimer;

protected:
	virtual OutputPin& getPin() = 0;
	virtual float getFanOnTemp() = 0;
	virtual float getFanOffTemp() = 0;
	virtual bool enableWithAc() = 0;
	virtual bool disableWhenStopped() = 0;
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
};
