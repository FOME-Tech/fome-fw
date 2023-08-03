/**
 * @file    interpolation.cpp
 * @brief	Linear interpolation algorithms
 *
 * See test_interpolation_3d.cpp
 *
 *
 * @date Oct 17, 2013
 * @author Andrey Belomutskiy, (c) 2012-2020
 * @author Dmitry Sidin, (c) 2015
 */

#include "pch.h"

#include "efi_interpolation.h"

#define BINARY_PERF true

#if BINARY_PERF && ! EFI_UNIT_TEST

#define COUNT 10000

float array16[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };

static void testBinary() {
	const int size16 = 16;

	uint32_t totalOld = 0;
	uint32_t totalNew = 0;

	for (int v = 0; v <= 16; v++) {
		uint32_t timeOld;
		{
			uint32_t start = getTimeNowLowerNt();
			int temp = 0;
			for (int i = 0; i < COUNT; i++) {
				temp += findIndex(array16, size16, v);
			}
			timeOld = getTimeNowLowerNt() - start;
		}
		uint32_t timeNew;
		{
			uint32_t start = getTimeNowLowerNt();
			int temp = 0;
			for (int i = 0; i < COUNT; i++) {
				temp += findIndex2(array16, size16, v);
			}
			timeNew = getTimeNowLowerNt() - start;
		}
		efiPrintf("for v=%d old=%d ticks", v, timeOld);
		efiPrintf("for v=%d new=%d ticks", v, timeNew);

		totalOld += timeOld;
		totalNew += timeNew;
	}
	efiPrintf("totalOld=%d ticks", totalOld);
	efiPrintf("totalNew=%d ticks", totalNew);

}

#endif

/** @brief	Linear interpolation by two points
 *
 * @param	x1 key of the first point
 * @param	y1 value of the first point
 * @param	x2 key of the second point
 * @param	y2 value of the second point
 * @param	X key to be interpolated
 *
 * @note	For example, "interpolateMsg("", engineConfiguration.tpsMin, 0, engineConfiguration.tpsMax, 100, adc);"
 * @see interpolateClamped
 */
float interpolateMsg(const char *msg, float x1, float y1, float x2, float y2, float x) {
	if (cisnan(x1) || cisnan(x2) || cisnan(y1) || cisnan(y2)) {
		warning(ObdCode::CUSTOM_ERR_INTERPOLATE_1, "interpolate%s: why param", msg);
		return NAN;
	}
	if (cisnan(x)) {
		warning(ObdCode::CUSTOM_ERR_INTERPOLATE_2, "interpolate%s: why X", msg);
		return NAN;
	}
	// todo: double comparison using EPS
	if (x1 == x2) {
		/**
		 * we could end up here for example while resetting bins while changing engine type
		 */
		warning(ObdCode::CUSTOM_ERR_INTERPOLATE_3, "interpolate%s: Same x1 and x2 in interpolate: %.2f/%.2f", msg, x1, x2);
		return NAN;
	}

	// a*x1 + b = y1
	// a*x2 + b = y2
//	efiAssertVoid(ObdCode::CUSTOM_ERR_ASSERT_VOID, x1 != x2, "no way we can interpolate");
	float a = INTERPOLATION_A(x1, y1, x2, y2);
	if (cisnan(a)) {
		warning(ObdCode::CUSTOM_ERR_INTERPOLATE_4, "interpolate%s: why a", msg);
		return NAN;
	}
	float b = y1 - a * x1;
	float result = a * x + b;
#if	DEBUG_FUEL
	printf("x1=%.2f y1=%.2f x2=%.2f y2=%.2f\r\n", x1, y1, x2, y2);
	printf("a=%.2f b=%.2f result=%.2f\r\n", a, b, result);
#endif
	return result;
}

/**
 * @see interpolateMsg
 */
float interpolateClamped(float x1, float y1, float x2, float y2, float x) {
	if (x <= x1)
		return y1;
	if (x >= x2)
		return y2;

	float a = INTERPOLATION_A(x1, y1, x2, y2);
	float b = y1 - a * x1;
	return a * x + b;
}

/**
 * Another implementation, which one is faster?
 */
int findIndex2(const float array[], unsigned size, float value) {
	efiAssert(ObdCode::CUSTOM_ERR_ASSERT, !cisnan(value), "NaN in findIndex2", 0);
	efiAssert(ObdCode::CUSTOM_ERR_ASSERT, size > 1, "size in findIndex", 0);
//	if (size <= 1)
//		return size && *array <= value ? 0 : -1;

	signed i = 0;
	//unsigned b = 1 << int(log(float(size) - 1) / 0.69314718055994530942);
	unsigned b = size >> 1; // in our case size is always a power of 2
	efiAssert(ObdCode::CUSTOM_ERR_ASSERT, b + b == size, "Size not power of 2", -1);
	for (; b; b >>= 1) {
		unsigned j = i | b;
		/**
		 * it should be
		 * "if (j < size && array[j] <= value)"
		 * but in our case size is always power of 2 thus size is always more then j
		 */
		// efiAssert(ObdCode::CUSTOM_ERR_ASSERT, j < size, "size", 0);
		if (array[j] <= value)
			i = j;
	}
	return i || *array <= value ? i : -1;
}

int findIndex(const float array[], int size, float value) {
	return findIndexMsg("", array, size, value);
}

void initInterpolation() {
#if BINARY_PERF && ! EFI_UNIT_TEST
	addConsoleAction("binarytest", testBinary);
#endif
}
