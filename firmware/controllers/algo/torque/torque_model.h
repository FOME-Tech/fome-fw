#pragma once

#include "torque_model_generated.h"

struct TorqueModelBase : public EngineModule, public torque_model_s {
public:
	using interface_t = TorqueModelBase;

	virtual float driverDemand() = 0;

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
	void onFastCallback() override;

	float driverDemand() override;
	percent_t getThrottleRequest() override;

	AirmassDispatcher airmassDispatcher;
};
