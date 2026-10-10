#pragma once

#include "airmass.h"

class MafAirmass final : public AirmassVeModelBase {
public:
	explicit MafAirmass(const ValueProvider3D* veTable = nullptr)
		: AirmassVeModelBase(veTable) {}

	expected<AirmassResult> getAirmass(float rpm, bool postState) override;

	// Compute airmass based on flow & engine speed
	expected<AirmassResult> getAirmassImpl(float massAirFlow, float rpm) const;

private:
	expected<float> getMaf() const;
};
