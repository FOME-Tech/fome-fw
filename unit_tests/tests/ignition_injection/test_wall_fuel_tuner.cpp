#include "pch.h"
#include "wall_fuel_tuner.h"

#include <cmath>
#include <complex>

namespace {
// Reference implementation of the model the fit is trying to recover:
// wall film (1 - beta*e^{-t/tau}) convolved with first-order sensor lag taus and
// pure transport delay of delayBins bins.  Produces a normalized 0->1 curve.
void synthResponse(float* out, int n, float dt, float tau, float taus, float beta, int delayBins) {
	float gain = tau / (tau - taus);
	for (int b = 0; b < n; b++) {
		if (b < delayBins) {
			out[b] = 0;
			continue;
		}
		float t = (b - delayBins) * dt;
		float A = 1 - std::exp(-t / taus);
		float B = -gain * (std::exp(-t / tau) - std::exp(-t / taus));
		out[b] = A + beta * B;
	}
}
} // namespace

// Clean synthetic data whose true parameters sit on the search grid: recovery
// should be essentially exact.
TEST(WallFuelTuner, fitExact) {
	constexpr int n = 60;
	constexpr float dt = 0.025f;

	const float trueTau = 0.241f;
	const float trueTaus = 0.0505f;
	const float trueBeta = 0.4f;
	const int trueDelay = 2;

	float s[n];
	synthResponse(s, n, dt, trueTau, trueTaus, trueBeta, trueDelay);

	WallModelFit fit = fitWallModel(s, n, dt);

	EXPECT_NEAR(fit.tau, trueTau, 0.03f);
	EXPECT_NEAR(fit.beta, trueBeta, 0.05f);
	EXPECT_NEAR(fit.sensorTau, trueTaus, 0.02f);
	EXPECT_NEAR(fit.deadTimeSec, trueDelay * dt, dt);
	EXPECT_LT(fit.rmsResidual, 0.02f);
}

// Off-grid true parameters plus noise: recovery should still be in the ballpark.
TEST(WallFuelTuner, fitWithNoise) {
	constexpr int n = 60;
	constexpr float dt = 0.025f;

	const float trueTau = 0.18f;
	const float trueTaus = 0.06f;
	const float trueBeta = 0.35f;
	const int trueDelay = 3;

	float s[n];
	synthResponse(s, n, dt, trueTau, trueTaus, trueBeta, trueDelay);

	// Deterministic pseudo-noise so the test is repeatable.  ~1% of full scale,
	// representative of residual noise on an ensemble-averaged curve.
	uint32_t seed = 12345;
	for (int b = 0; b < n; b++) {
		seed = seed * 1664525u + 1013904223u;
		float noise = ((seed >> 8) & 0xFFFF) / 65535.0f - 0.5f; // [-0.5, 0.5)
		s[b] += noise * 0.01f;
	}

	WallModelFit fit = fitWallModel(s, n, dt);

	// tau is the well-observed slow mode and should track tightly.  beta lives in
	// the step edge (the sensor-lag-confounded part), so noise perturbs it much
	// more - we only sanity-bound it rather than pin it.  This asymmetry is
	// fundamental, not a bug.
	EXPECT_NEAR(fit.tau, trueTau, 0.05f);
	EXPECT_GT(fit.beta, 0.1f);
	EXPECT_LT(fit.beta, 0.7f);
	EXPECT_LT(fit.rmsResidual, 0.05f);
}

namespace {
// The model's complex frequency response (fuel -> cylinder fuel) that the
// multisine fit recovers.
std::complex<float> synthH(float f, float tau, float taus, float beta, float td) {
	using cf = std::complex<float>;
	float w = 2 * static_cast<float>(M_PI) * f;
	cf delay = std::polar(1.0f, -w * td);
	cf sensor = cf(1, 0) / cf(1, w * taus);
	cf wall = cf(1, (1 - beta) * w * tau) / cf(1, w * tau);
	return delay * sensor * wall;
}
} // namespace

TEST(WallFuelTuner, fitFreqExact) {
	constexpr int n = WallFuelTuner::MS_NTONES;
	const int harmonics[n] = {1, 2, 3, 5, 8, 13, 21, 34};
	float freq[n];
	for (int k = 0; k < n; k++) {
		freq[k] = harmonics[k] / WallFuelTuner::MS_BASE_PERIOD_S;
	}

	const float trueTau = 0.241f;
	const float trueTaus = 0.0505f;
	const float trueBeta = 0.4f;
	const float trueTd = 0.04f;

	float hRe[n], hIm[n];
	for (int k = 0; k < n; k++) {
		auto H = synthH(freq[k], trueTau, trueTaus, trueBeta, trueTd);
		hRe[k] = H.real();
		hIm[k] = H.imag();
	}

	WallModelFit fit = fitWallModelFreq(freq, hRe, hIm, n);

	EXPECT_NEAR(fit.tau, trueTau, 0.04f);
	EXPECT_NEAR(fit.beta, trueBeta, 0.07f);
	EXPECT_NEAR(fit.sensorTau, trueTaus, 0.025f);
	EXPECT_NEAR(fit.deadTimeSec, trueTd, 0.025f);
	EXPECT_LT(fit.rmsResidual, 0.02f);
}
