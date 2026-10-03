#pragma once

#include "rusefi_types.h"
#include <cstdint>

struct AirmassInputs;

// Only static calibration validity belongs here. Sensors, geometry and active
// dependencies are evaluated on every pass. Owned by Engine so fixtures and
// engine replacement cannot reuse another engine's cache.
enum class AirmassCalibration : uint8_t {
	SpeedDensity,
	AlphaN,
	Maf,
	MapEstimate,
	Authority,
	Idle,
	Correction0,
};

struct AirmassCalibrationCache {
	uint32_t Generation = 1;
	uint32_t Known = 0;
	uint32_t Valid = 0;
	int ConfigurationVersion = -1;
	float ChargeDisplacement = 0;
	float ChargeCylinderCount = -1;
	float StandardCharge = 0;
#if EFI_UNIT_TEST
	uint32_t Scans = 0;
#endif
};

#if !EFI_UNIT_TEST
static_assert(sizeof(AirmassCalibrationCache) == 28);
#endif

// Call before mutating calibration. Does not change the burn version.
void invalidateAirmassCalibration();
bool isCapturedAirmassCalibrationValid(const AirmassInputs& inputs, AirmassCalibration calibration);
bool isCapturedAirmassModelValid(const AirmassInputs& inputs, engine_load_mode_e model);
