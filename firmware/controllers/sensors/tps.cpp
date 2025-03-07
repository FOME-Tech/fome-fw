/**
 * @author Andrey Belomutskiy, (c) 2012-2020
 */
#include "pch.h"

void grabTPSIsClosed() {
#if EFI_PROD_CODE
	engineConfiguration->tpsMin = convertVoltageTo10bitADC(Sensor::getRaw(SensorType::Tps1));
#endif /* EFI_PROD_CODE */
}

void grabTPSIsWideOpen() {
#if EFI_PROD_CODE
	engineConfiguration->tpsMax = convertVoltageTo10bitADC(Sensor::getRaw(SensorType::Tps1));
#endif /* EFI_PROD_CODE */
}

void grabPedalIsUp() {
	/**
	 * search for 'maintainConstantValue' to find how this TS magic works
	 */
	engine->outputChannels.calibrationMode = (uint8_t)TsCalMode::PedalMin;
	engine->outputChannels.calibrationValue = Sensor::getRaw(SensorType::AcceleratorPedalPrimary);
	engine->outputChannels.calibrationValue2 = Sensor::getRaw(SensorType::AcceleratorPedalSecondary);
}

void grabPedalIsWideOpen() {
	engine->outputChannels.calibrationMode = (uint8_t)TsCalMode::PedalMax;
	engine->outputChannels.calibrationValue = Sensor::getRaw(SensorType::AcceleratorPedalPrimary);
	engine->outputChannels.calibrationValue2 = Sensor::getRaw(SensorType::AcceleratorPedalSecondary);
}
