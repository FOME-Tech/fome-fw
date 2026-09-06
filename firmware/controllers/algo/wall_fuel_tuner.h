/*
 * @file wall_fuel_tuner.h
 *
 * Experimental self-identification of the wall-wetting (fuel film) model
 * parameters tau and beta.  See wall_fuel.cpp for the model itself.
 *
 * The idea: hold airflow constant (steady idle / held RPM) and perturb the
 * commanded fuel mass with a square wave.  At constant air, lambda is a near
 * direct readout of cylinder fuel mass, so the fuel->lambda response identifies
 * the fuel-film plant.  We ensemble-average the response to many square-wave
 * edges, then fit the structured model
 *
 *     m(t) = 1 - e^{-t'/taus} - beta*tau/(tau-taus)*(e^{-t'/tau} - e^{-t'/taus})
 *
 * (wall film convolved with a first-order sensor/exhaust lag taus and transport
 * delay Td) to recover tau and beta.
 *
 * Two excitation modes share the same model:
 *   - 'wwtune'   step (square-wave) excitation, time-domain ensemble fit.  Fast,
 *                so it works on a cold/warming engine where the plant drifts.
 *   - 'wwtunems' multisine excitation, frequency-domain fit.  Slower but higher
 *                SNR and cleaner phase; best on a warm, steady engine.
 *
 * Console-driven diagnostics; results are printed, nothing is written back to
 * the configuration.
 */

#pragma once

#include "engine_module.h"

// Result of fitting the wall-film + sensor-lag model.
struct WallModelFit {
	float tau;		   // wall film time constant, seconds (-> wwaeTau)
	float beta;		   // wall deposit fraction (-> wwaeBeta)
	float sensorTau;   // lumped lambda/exhaust lag, seconds (byproduct)
	float deadTimeSec; // transport delay, seconds (byproduct)
	float rmsResidual; // fit residual (normalized)
};

// Fit {tau, beta, sensorTau, delay} to a normalized (0->1) step response sampled
// at binWidthSec spacing.  Pure function, exposed for unit testing.
WallModelFit fitWallModel(const float* normalizedResponse, int n, float binWidthSec);

// Fit {tau, beta, sensorTau, deadTime} to a measured complex frequency response
// (fuel -> cylinder fuel, DC gain ~1) sampled at the given frequencies.  Used by
// the multisine mode.  Pure function, exposed for unit testing.
WallModelFit fitWallModelFreq(const float* freqHz, const float* hReal, const float* hImag, int nFreq);

class WallFuelTuner : public EngineModule {
public:
	// Max number of ensemble time bins (half-period / binWidth).
	static constexpr int MAX_BINS = 64;
	// Width of one ensemble time bin, milliseconds.
	static constexpr float BIN_WIDTH_MS = 25;

	// Multisine excitation: number of tones, and the base period (1/f0) whose
	// reciprocal is the lowest tone.  Tones are integer harmonics of f0.
	static constexpr int MS_NTONES = 8;
	static constexpr float MS_BASE_PERIOD_S = 10; // f0 = 0.1 Hz

	using interface_t = WallFuelTuner;

	void onFastCallback() override;
	void onSlowCallback() override;
	void onEngineStop() override;

	// Arm the step (square-wave) test.  amplitude is the +/- fuel perturbation
	// fraction (e.g. 0.06), halfPeriodMs the square-wave half period, periods the
	// number of full cycles to average.  Zero arguments fall back to defaults.
	void start(float amplitude, float halfPeriodMs, int periods);

	// Arm the multisine test.  amplitude is the peak fuel perturbation fraction;
	// periods is the number of base periods (the first is discarded for settling,
	// the rest averaged).  Best for a warm, steady engine - it trades runtime for
	// SNR and clean phase, identifying the same model in the frequency domain.
	void startMultisine(float amplitude, int periods);

	// True while the perturbation is being applied.  Consulted to disable the
	// wall-wetting correction and STFT closed-loop fuel so we measure the bare
	// open-loop plant.
	bool isActive() const {
		return m_state == State::Collecting;
	}

	// Multiplicative fuel perturbation to apply to commanded fuel, 1.0 when idle.
	float getFuelMult() const {
		return m_fuelMult;
	}

private:
	enum class State {
		Idle,
		Collecting,
		Fit, // collection done, fit pending (runs in slow callback)
	};

	enum class Mode {
		Step,
		Multisine,
	};

	bool startCommon(float amplitude);
	void reset();
	void accumulate(float lambda, int binIndex, int signOfEdge);
	void stepIteration(float elapsedMs);
	void multisineIteration(float elapsedS);
	void computeAndReport();
	void computeAndReportMultisine();

	State m_state = State::Idle;
	Mode m_mode = Mode::Step;

	// Test parameters
	float m_amplitude = 0;
	float m_halfPeriodMs = 0;
	int m_periods = 0;
	int m_numBins = 0;
	int m_settleHalfPeriods = 0;

	float m_fuelMult = 1;

	// Timing
	Timer m_runTimer;

	// Step mode: ensemble accumulators, indexed by time-since-edge bin.  Signed by
	// edge direction so rising and falling fuel edges reinforce.
	float m_sum[MAX_BINS];
	uint16_t m_count[MAX_BINS];

	// Multisine mode: per-tone quadrature accumulators for both the known input u
	// and the measured lambda y.  H = (Y/U) gives the complex response per tone.
	float m_msFreq[MS_NTONES];
	float m_msPhase[MS_NTONES];
	float m_msScale = 0;
	float m_msYc[MS_NTONES];
	float m_msYs[MS_NTONES];
	float m_msUc[MS_NTONES];
	float m_msUs[MS_NTONES];

	// Diagnostics gathered during collection
	float m_lambdaSum = 0;
	uint32_t m_lambdaCount = 0;
	float m_rpmMin = 0;
	float m_rpmMax = 0;
	uint32_t m_badLambdaCount = 0;
};
