/*
 * @file advance_map.h
 *
 * @date Mar 27, 2013
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "ignition_state_generated.h"

#pragma once

angle_t getAdvance(float rpm, float engineLoad);
angle_t getCylinderIgnitionTrim(size_t cylinderNumber, float rpm, float ignitionLoad);
/**
 * this method is used to build default advance map
 */
float getInitialAdvance(float rpm, float map, float advanceMax);

size_t getMultiSparkCount(float rpm);

class IgnitionState : public ignition_state_s {
public:
	floatms_t getSparkDwell(float rpm);
};
