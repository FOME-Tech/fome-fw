#pragma once

#include "airmass.h"
#include "efi_timer.h"

class MafAirmass final : public AirmassVeModelBase {
public:
	explicit MafAirmass(const ValueProvider3D* veTable = nullptr)
		: AirmassVeModelBase(veTable) {}

	expected<AirmassResult> getAirmass(float rpm, bool postState) override;

	// Compute airmass based on flow & engine speed, without the intake manifold filter
	expected<AirmassResult> getAirmassImpl(float massAirFlow, float rpm) const;

private:
	expected<float> getMaf() const;

	// Per-cylinder airmass implied by a flow and engine speed, limited by the max load curve
	mass_t getMeasuredCylinderAirmass(float massAirFlow, float rpm) const;

	// Apply the MAF trim table, and compute the load for this airmass
	AirmassResult makeResult(mass_t cylinderAirmass, float rpm) const;

	// Intake manifold filter, see getAirmass()
	mass_t filter(mass_t measuredAirmass, float rpm);

	bool m_hasFilterState = false;
	mass_t m_filteredAirmass = 0;
	Timer m_filterTimer;
};
