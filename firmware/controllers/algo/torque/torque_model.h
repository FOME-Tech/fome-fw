#pragma once

#include "torque_model_generated.h"

struct TorqueModelBase : public EngineModule, public torque_model_s {
public:
	using interface_t = TorqueModelBase;

	// Orchestration: collect demand, limit, add loss, convert to an airmass target, and
	// hand it to the airmass sink. The individual steps are virtual so they can be tested
	// (and mocked) in isolation - this method is just the wiring between them.
	void onFastCallback() override final;

	virtual float driverDemand() const = 0;
	virtual float getTorqueLoss() = 0;
	virtual float applyTorqueLimits(float torqueRequested) = 0;

	// Sink for the computed per-cycle airmass target. The real model drives the ETB through
	// the airmass dispatcher; tests can capture the target without any airpath setup.
	virtual void commandAirmass(float airmassTarget) = 0;

	// Output to ETB
	virtual percent_t getThrottleRequest() = 0;
};

class AirmassDispatcher {
public:
	// targetAirmass is the desired per-cycle airmass [g/cycle]
	void update(float targetAirmassPerCycle);

	percent_t getThrottleRequest() const;
	float getAirmassTrim() const;
	float getActualAirmass() const;

private:
	percent_t m_throttleRequest = 0;
	float m_airmassTrim = 0;
	float m_actualAirmass = 0;

	// Integrator accumulator for the hand-rolled airmass-trim PI
	float m_trimITerm = 0;
};

class TorqueModel : public TorqueModelBase {
public:
	float driverDemand() const override;
	float getTorqueLoss() override;
	float applyTorqueLimits(float torqueRequested) override;
	void commandAirmass(float airmassTarget) override;

	// Outputs
	percent_t getThrottleRequest() override;

	AirmassDispatcher airmassDispatcher;
};
