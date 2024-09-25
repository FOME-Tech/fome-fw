#pragma once

#include "airmass.h"

class MafAirmass final : public AirmassVeModelBase {
public:
	explicit MafAirmass(const ValueProvider3D* veTable = nullptr) : AirmassVeModelBase(veTable) {}

	AirmassResult getAirmass(float rpm, bool postState) override;

	// Compute airmass based on flow & engine speed
	AirmassResult getAirmassImpl(float massAirFlow, float rpm, bool postState) const;

	float getVeImpl(float rpm, percent_t load) const override;

private:
	float getMaf() const;
};
