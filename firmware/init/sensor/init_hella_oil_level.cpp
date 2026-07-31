#include "pch.h"

#include "init.h"
#include "hella_oil_level.h"

// One sensor drives both channels - level and temperature come from the same signal
static HellaOilLevelSensor hellaOilLevel(SensorType::OilLevel, SensorType::OilTemperature);

void initHellaOilLevel() {
	hellaOilLevel.init(engineConfiguration->hellaOilLevelPin, engineConfiguration->hellaOilLevelVariant);
}

void deInitHellaOilLevel() {
	hellaOilLevel.deInit();
}
