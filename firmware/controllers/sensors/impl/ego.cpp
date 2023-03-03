/**
 * @author Andrey Belomutskiy, (c) 2012-2020
 *
 * EGO Exhaust Gas Oxygen, also known as AFR Air/Fuel Ratio :)
 *
 * rusEfi has three options for wideband:
 * 1) integration with external widebands using liner analog signal wire
 * 2) 8-point interpolation curve to emulate a wide-band with a narrow-band sensor.
 * 3) CJ125 internal wideband controller is known to work with both 4.2 and 4.9
 *
 */
#include "pch.h"

#include "cyclic_buffer.h"

bool hasAfrSensor() {
	if (engineConfiguration->enableAemXSeries || engineConfiguration->enableInnovateLC2) {
		return true;
	}

	return isAdcChannelValid(engineConfiguration->afr.hwChannel);
}

extern float InnovateLC2AFR;

float getAfr(SensorType type) {
#if EFI_AUX_SERIAL
	if (engineConfiguration->enableInnovateLC2)
		return InnovateLC2AFR;
#endif

	afr_sensor_s * sensor = &engineConfiguration->afr;

	if (!isAdcChannelValid(type == SensorType::Lambda1 ? engineConfiguration->afr.hwChannel : engineConfiguration->afr.hwChannel2)) {
		return 0;
	}

	float volts = getVoltageDivided("ego", type == SensorType::Lambda1 ? sensor->hwChannel : sensor->hwChannel2);

	return interpolateMsg("AFR", sensor->v1, sensor->value1, sensor->v2, sensor->value2, volts)
			+ engineConfiguration->egoValueShift;
}

void setEgoSensor(ego_sensor_e type) {
	auto sensor = &engineConfiguration->afr;

	switch (type) {
	case ES_BPSX_D1:
		/**
		 * This decodes BPSX D1 Wideband Controller analog signal
		 */
		sensor->v1 = 0;
		sensor->value1 = 9;
		sensor->v2 = 5;
		sensor->value2 = 19;
		break;

	case ES_Innovate_MTX_L:
		sensor->v1 = 0;
		sensor->value1 = 7.35;
		sensor->v2 = 5;
		sensor->value2 = 22.39;
		break;
	case ES_14Point7_Free:
		sensor->v1 = 0;
		sensor->value1 = 9.996;
		sensor->v2 = 5;
		sensor->value2 = 19.992;
		break;
		// technically 14Point7 and PLX use the same scale
	case ES_PLX:
		sensor->v1 = 0;
		sensor->value1 = 10;
		sensor->v2 = 5;
		sensor->value2 = 20;
		break;
	default:
		firmwareError(CUSTOM_EGO_TYPE, "Unexpected EGO %d", type);
		break;
	}
}
