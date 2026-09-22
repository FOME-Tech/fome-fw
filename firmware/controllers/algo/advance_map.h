/*
 * @file advance_map.h
 *
 * @date Mar 27, 2013
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#pragma once

template <unsigned TRowCount, unsigned TColumnCount>
class PreparedTable3DInterpolation;

angle_t
getCylinderIgnitionTrim(size_t cylinderNumber, const PreparedTable3DInterpolation<TRIM_SIZE, TRIM_SIZE>& interpolation);
/**
 * this method is used to build default advance map
 */
float getInitialAdvance(float rpm, float map, float advanceMax);

size_t getMultiSparkCount(float rpm);
