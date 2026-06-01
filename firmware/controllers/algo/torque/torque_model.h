#pragma once

#include "torque_model_generated.h"

struct TorqueModelBase : public EngineModule, public torque_model_s {
public:
	using interface_t = TorqueModelBase;

	virtual float driverDemand() = 0;
};

class TorqueModel : public TorqueModelBase {
public:
	float driverDemand() override;
};
