#pragma once

#include "ignition_state_generated.h"

class IgnitionState : public ignition_state_s {
public:
	void updateDwell(float rpm, bool isCranking);
	floatms_t getDwell() const;

	void updateAdvanceCorrections(float engineLoad);

	angle_t getAdvance(float rpm, float engineLoad, bool isCranking);

private:
	angle_t getAdvanceCorrections(bool isCranking) const;

	floatms_t getSparkDwell(float rpm, bool isCranking);
};
