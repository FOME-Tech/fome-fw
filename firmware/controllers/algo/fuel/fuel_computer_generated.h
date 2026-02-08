#pragma once
#include "rusefi_types.h"
struct running_fuel_s {
	// Fuel: Post cranking mult
	// offset 0
	float postCrankingFuelCorrection = (float)0;
	// Fuel: IAT correction
	// offset 4
	float intakeTemperatureCoefficient = (float)0;
	// Fuel: CLT correction
	// offset 8
	float coolantTemperatureCoefficient = (float)0;
	// secs
	// offset 12
	float timeSinceCrankingInSecs = (float)0;
	// Fuel: base cycle mass
	// mg
	// offset 16
	scaled_channel<uint16_t, 100, 1> baseFuel = (uint16_t)0;
	// Fuel: running
	// mg
	// offset 18
	scaled_channel<uint16_t, 100, 1> fuel = (uint16_t)0;
};
static_assert(sizeof(running_fuel_s) == 20);
static_assert(offsetof(running_fuel_s, postCrankingFuelCorrection) == 0);
static_assert(offsetof(running_fuel_s, intakeTemperatureCoefficient) == 4);
static_assert(offsetof(running_fuel_s, coolantTemperatureCoefficient) == 8);
static_assert(offsetof(running_fuel_s, timeSinceCrankingInSecs) == 12);
static_assert(offsetof(running_fuel_s, baseFuel) == 16);
static_assert(offsetof(running_fuel_s, fuel) == 18);
struct fuel_computer_s {
	// offset 0
	running_fuel_s running;
	// Fuel: Total correction
	// mult
	// offset 20
	scaled_channel<uint16_t, 100, 1> totalFuelCorrection = (uint16_t)0;
	// %
	// offset 22
	scaled_channel<uint16_t, 100, 1> afrTableYAxis = (uint16_t)0;
	// Fuel: target lambda
	// offset 24
	scaled_channel<uint16_t, 10000, 1> targetLambda = (uint16_t)0;
	// Fuel: target AFR
	// ratio
	// offset 26
	scaled_channel<uint16_t, 1000, 1> targetAFR = (uint16_t)0;
	// Fuel: Stoich ratio
	// ratio
	// offset 28
	scaled_channel<uint16_t, 1000, 1> stoichiometricRatio = (uint16_t)0;
	// offset 30
	uint8_t alignmentFill_at_30[2];
	// offset 32
	float sdTcharge_coff = (float)0;
	// Air: Cylinder airmass
	// g
	// offset 36
	float sdAirMassInOneCylinder = (float)0;
	// Air: Normalized cyl filling
	// %
	// offset 40
	float normalizedCylinderFilling = (float)0;
};
static_assert(sizeof(fuel_computer_s) == 44);
static_assert(offsetof(fuel_computer_s, totalFuelCorrection) == 20);
static_assert(offsetof(fuel_computer_s, afrTableYAxis) == 22);
static_assert(offsetof(fuel_computer_s, targetLambda) == 24);
static_assert(offsetof(fuel_computer_s, targetAFR) == 26);
static_assert(offsetof(fuel_computer_s, stoichiometricRatio) == 28);
static_assert(offsetof(fuel_computer_s, sdTcharge_coff) == 32);
static_assert(offsetof(fuel_computer_s, sdAirMassInOneCylinder) == 36);
static_assert(offsetof(fuel_computer_s, normalizedCylinderFilling) == 40);
