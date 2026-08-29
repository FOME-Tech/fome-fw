/**
 * @file fuel_computer.h
 */

#pragma once

class ValueProvider3D;

#include "rusefi_types.h"
#include "fuel_computer_generated.h"

struct IFuelComputer : public fuel_computer_s {
	virtual mass_t getCycleFuel(mass_t airmass, float rpm, float load) = 0;
	temperature_t getTCharge(float rpm, float tps);
	float getLoadOverride(float defaultLoad, load_override_e overrideMode) const;

private:
	float getTChargeCoefficient(float rpm, float tps);
};

// This contains the math of the fuel model, but doesn't actually read any configuration
class FuelComputerBase : public IFuelComputer {
public:
	mass_t getCycleFuel(mass_t airmass, float rpm, float load) override;

	virtual float getStoichiometricRatio() const = 0;
	virtual float getTargetLambda(float rpm, float load) const = 0;
	virtual float getTargetLambdaLoadAxis(float defaultLoad) const = 0;
};

// This class is a usable implementation of a fuel model that reads real configuration
class FuelComputer final : public FuelComputerBase {
public:
	float getStoichiometricRatio() const override;
	float getTargetLambda(float rpm, float load) const override;
	float getTargetLambdaLoadAxis(float defaultLoad) const override;
};

float getLoadOverride(float defaultLoad, load_override_e overrideMode);

// Last valid flex fuel sensor reading stored in backup RAM, if there is one
expected<float> getStoredFlexEthanolPercent();

// Best ethanol content we know of: the live sensor if it's valid, otherwise the last stored
// reading, otherwise the configured default
float getFlexEthanolPercent();

// Store the current flex fuel sensor reading in backup RAM, if it's valid
void updateStoredFlexEthanolPercent();

constexpr float fuelDensity = 0.72; // g/cc
