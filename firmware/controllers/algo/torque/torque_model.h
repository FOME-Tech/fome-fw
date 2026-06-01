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
	void update(float airmassTarget);

	percent_t getThrottleRequest();
};

class TorqueModel : public TorqueModelBase {
public:
	void onFastCallback() override;

	float driverDemand() override;
	percent_t getThrottleRequest() override;

	AirmassDispatcher airmassDispatcher;
};
