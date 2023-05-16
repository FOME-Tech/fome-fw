#pragma once

#include "maf_airmass.h"

class ThrottleModelAirmass : public MafAirmass {
public:
	explicit ThrottleModelAirmass(const ValueProvider3D& veTable) : MafAirmass(veTable) {}

	AirmassResult getAirmass(int rpm, bool postState) override;
};
