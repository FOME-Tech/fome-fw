/*
 * @file biquad.cpp
 *
 * @date Sep 10, 2016
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"

#include "biquad.h"

Biquad::Biquad() {
	// Default to passthru
	a0 = 1;
	a1 = a2 = b1 = b2 = 0;

	reset();
}

void Biquad::reset() {
	z1 = z2 = 0;
}

static float getK(float samplingFrequency, float cutoff) {
	return tanf_taylor(CONST_PI * cutoff / samplingFrequency);
}

static float getNorm(float K, float Q) {
	return 1 / (1 + K / Q + K * K);
}

/**
 * Coefficients are computed and stored as float32, which puts a floor on how slow a filter we can
 * usefully build.  As the cutoff shrinks relative to the sampling rate the poles crowd up against
 * z = 1, meaning b1 -> -2 and b2 -> 1.  The filter's DC gain is set by (1 + b1 + b2), which is then
 * a catastrophic cancellation: at fc = fs/10000 that sum is ~4e-7 while one ulp of b1 is ~2.4e-7,
 * so only a bit or two of it survives and the gain can land anywhere from 0.5x to 3x.  Worse, the
 * poles can round to just outside the unit circle and the filter diverges.
 *
 * fs/1000 keeps ~9 bits in that sum, which is enough for the numerator correction below to work.
 * To filter more slowly than that, decimate: run the filter from a slower periodic task.
 */
static bool checkFrequencies(float samplingFrequency, float frequency) {
	return samplingFrequency >= 2.5f * frequency && samplingFrequency <= 1000 * frequency;
}

void Biquad::configureBandpass(float samplingFrequency, float centerFrequency, float Q) {
	efiAssertVoid(
			ObdCode::OBD_PCM_Processor_Fault,
			checkFrequencies(samplingFrequency, centerFrequency),
			"Invalid biquad parameters");

	float K = getK(samplingFrequency, centerFrequency);
	float norm = getNorm(K, Q);

	a0 = K / Q * norm;
	a1 = 0;
	a2 = -a0;
	b1 = 2 * (K * K - 1) * norm;
	b2 = (1 - K / Q + K * K) * norm;
}

void Biquad::configureLowpass(float samplingFrequency, float cutoffFrequency, float Q) {
	efiAssertVoid(
			ObdCode::OBD_PCM_Processor_Fault,
			checkFrequencies(samplingFrequency, cutoffFrequency),
			"Invalid biquad parameters");

	float K = getK(samplingFrequency, cutoffFrequency);
	float norm = getNorm(K, Q);

	b1 = 2 * (K * K - 1) * norm;
	b2 = (1 - K / Q + K * K) * norm;

	// Algebraically this is K * K * norm, but computing it from the b coefficients we actually
	// stored means the DC gain, 4 * a0 / (1 + b1 + b2), comes out as exactly 1 no matter how the
	// cancellation described above rounded.
	a0 = (1 + b1 + b2) / 4;
	a1 = 2 * a0;
	a2 = a0;
}

void Biquad::configureHighpass(float samplingFrequency, float cutoffFrequency, float Q) {
	efiAssertVoid(
			ObdCode::OBD_PCM_Processor_Fault,
			checkFrequencies(samplingFrequency, cutoffFrequency),
			"Invalid biquad parameters");

	float K = getK(samplingFrequency, cutoffFrequency);
	float norm = getNorm(K, Q);

	a0 = 1 * norm;
	a1 = -2 * a0;
	a2 = a0;
	b1 = 2 * (K * K - 1) * norm;
	b2 = (1 - K / Q + K * K) * norm;
}

float Biquad::filter(float input) {
	float result = input * a0 + z1;
	z1 = input * a1 + z2 - b1 * result;
	z2 = input * a2 - b2 * result;
	return result;
}

void Biquad::cookSteadyState(float steadyStateInput) {
	float Y = steadyStateInput * (a0 + a1 + a2) / (1 + b1 + b2);

	float steady_z2 = steadyStateInput * a2 - Y * b2;
	float steady_z1 = steady_z2 + steadyStateInput * a1 - Y * b1;

	this->z1 = steady_z1;
	this->z2 = steady_z2;
}
