/*
 * @file wall_fuel_tuner.cpp
 *
 * @author Matthew Kennedy
 *
 * See wall_fuel_tuner.h for theory of operation.
 */

#include "pch.h"
#include "wall_fuel_tuner.h"

#include <cmath>
#include <complex>

// Grid for the parameter search (seconds).
static constexpr float TAU_MIN = 0.03f;
static constexpr float TAU_MAX = 1.5f;
static constexpr int TAU_STEPS = 16;

static constexpr float TAUS_MIN = 0.02f;
static constexpr float TAUS_MAX = 0.6f;
static constexpr int TAUS_STEPS = 12;

// Max transport delay searched, in bins (BIN_WIDTH_MS each).
static constexpr int MAX_DELAY_BINS = 12;

static float logSpaced(float lo, float hi, int i, int n) {
	if (n <= 1) {
		return lo;
	}
	return lo * expf((float)i / (n - 1) * logf(hi / lo));
}

WallModelFit fitWallModel(const float* s, int n, float dt) {
	// Grid search over (tau, taus, Td); beta solved in closed form at each node.
	// Model: m(t) = 1 - e^{-t'/taus} - beta*tau/(tau-taus)*(e^{-t'/tau} - e^{-t'/taus})
	int maxDelay = std::min(MAX_DELAY_BINS, n / 2);

	float bestSse = 1e30f;
	float bestTau = 0, bestTaus = 0, bestBeta = 0;
	int bestTd = 0;

	float eTaus[WallFuelTuner::MAX_BINS];
	float eTau[WallFuelTuner::MAX_BINS];

	for (int si = 0; si < TAUS_STEPS; si++) {
		float taus = logSpaced(TAUS_MIN, TAUS_MAX, si, TAUS_STEPS);
		for (int b = 0; b < n; b++) {
			eTaus[b] = expf(-b * dt / taus);
		}

		for (int ti = 0; ti < TAU_STEPS; ti++) {
			float tau = logSpaced(TAU_MIN, TAU_MAX, ti, TAU_STEPS);
			if (fabsf(tau - taus) < 1e-3f) {
				continue;
			}
			float gain = tau / (tau - taus);
			for (int b = 0; b < n; b++) {
				eTau[b] = expf(-b * dt / tau);
			}

			for (int td = 0; td <= maxDelay; td++) {
				// Closed-form least squares for beta:  s ~= A + beta * B
				float sBB = 0, sSmAB = 0;
				for (int b = 0; b < n; b++) {
					if (b < td) {
						continue; // A = B = 0
					}
					int k = b - td;
					float A = 1 - eTaus[k];
					float B = -gain * (eTau[k] - eTaus[k]);
					sBB += B * B;
					sSmAB += (s[b] - A) * B;
				}

				if (sBB < 1e-9f) {
					continue;
				}
				float beta = sSmAB / sBB;
				if (beta < 0) {
					beta = 0;
				} else if (beta > 1) {
					beta = 1;
				}

				float sse = 0;
				for (int b = 0; b < n; b++) {
					float A = 0, B = 0;
					if (b >= td) {
						int k = b - td;
						A = 1 - eTaus[k];
						B = -gain * (eTau[k] - eTaus[k]);
					}
					float r = s[b] - A - beta * B;
					sse += r * r;
				}

				if (sse < bestSse) {
					bestSse = sse;
					bestTau = tau;
					bestTaus = taus;
					bestTd = td;
					bestBeta = beta;
				}
			}
		}
	}

	WallModelFit result;
	result.tau = bestTau;
	result.beta = bestBeta;
	result.sensorTau = bestTaus;
	result.deadTimeSec = bestTd * dt;
	result.rmsResidual = sqrtf(bestSse / n);
	return result;
}

// Dead-time search grid for the frequency-domain fit (seconds).
static constexpr float TD_MAX_S = 0.3f;
static constexpr int TD_STEPS = 16;

WallModelFit fitWallModelFreq(const float* freqHz, const float* hReal, const float* hImag, int nFreq) {
	using cf = std::complex<float>;

	// Model (fuel -> cylinder fuel, DC gain 1):
	//   H(jw) = e^{-jw Td} / (1 + jw taus) * (1 + (1-beta) jw tau) / (1 + jw tau)
	//         = P + beta * Q,  linear in beta
	//   P = e^{-jw Td} / (1 + jw taus)
	//   Q = -e^{-jw Td} * [jw tau / (1 + jw tau)] * [1 / (1 + jw taus)]
	// Grid search over (tau, taus, Td); beta in closed form (real LS over complex
	// residuals).  Precompute the per-tone complex factors so the triple loop has
	// no transcendental calls.  static keeps these off the (small) callback stack.
	static cf invSensor[TAUS_STEPS][WallFuelTuner::MS_NTONES]; // 1/(1+jw taus)
	static cf aWall[TAU_STEPS][WallFuelTuner::MS_NTONES];	   // jw tau/(1+jw tau)
	static cf delay[TD_STEPS][WallFuelTuner::MS_NTONES];	   // e^{-jw Td}

	float w[WallFuelTuner::MS_NTONES];
	for (int j = 0; j < nFreq; j++) {
		w[j] = 2 * CONST_PI * freqHz[j];
	}

	for (int si = 0; si < TAUS_STEPS; si++) {
		float taus = logSpaced(TAUS_MIN, TAUS_MAX, si, TAUS_STEPS);
		for (int j = 0; j < nFreq; j++) {
			invSensor[si][j] = cf(1, 0) / cf(1, w[j] * taus);
		}
	}
	for (int ti = 0; ti < TAU_STEPS; ti++) {
		float tau = logSpaced(TAU_MIN, TAU_MAX, ti, TAU_STEPS);
		for (int j = 0; j < nFreq; j++) {
			aWall[ti][j] = cf(0, w[j] * tau) / cf(1, w[j] * tau);
		}
	}
	for (int di = 0; di < TD_STEPS; di++) {
		float td = TD_MAX_S * di / (TD_STEPS - 1);
		for (int j = 0; j < nFreq; j++) {
			delay[di][j] = std::polar(1.0f, -w[j] * td);
		}
	}

	float bestSse = 1e30f;
	float bestTau = 0, bestTaus = 0, bestBeta = 0, bestTd = 0;

	for (int si = 0; si < TAUS_STEPS; si++) {
		for (int ti = 0; ti < TAU_STEPS; ti++) {
			for (int di = 0; di < TD_STEPS; di++) {
				float sQQ = 0, sReQd = 0;
				for (int j = 0; j < nFreq; j++) {
					cf P = delay[di][j] * invSensor[si][j];
					cf Q = -delay[di][j] * aWall[ti][j] * invSensor[si][j];
					cf d = cf(hReal[j], hImag[j]) - P;
					sQQ += std::norm(Q);
					sReQd += std::real(std::conj(Q) * d);
				}
				if (sQQ < 1e-12f) {
					continue;
				}
				float beta = sReQd / sQQ;
				if (beta < 0) {
					beta = 0;
				} else if (beta > 1) {
					beta = 1;
				}

				float sse = 0;
				for (int j = 0; j < nFreq; j++) {
					cf P = delay[di][j] * invSensor[si][j];
					cf Q = -delay[di][j] * aWall[ti][j] * invSensor[si][j];
					cf res = cf(hReal[j], hImag[j]) - P - beta * Q;
					sse += std::norm(res);
				}

				if (sse < bestSse) {
					bestSse = sse;
					bestTau = logSpaced(TAU_MIN, TAU_MAX, ti, TAU_STEPS);
					bestTaus = logSpaced(TAUS_MIN, TAUS_MAX, si, TAUS_STEPS);
					bestTd = TD_MAX_S * di / (TD_STEPS - 1);
					bestBeta = beta;
				}
			}
		}
	}

	WallModelFit result;
	result.tau = bestTau;
	result.beta = bestBeta;
	result.sensorTau = bestTaus;
	result.deadTimeSec = bestTd;
	result.rmsResidual = sqrtf(bestSse / nFreq);
	return result;
}

void WallFuelTuner::reset() {
	m_state = State::Idle;
	m_fuelMult = 1;
	m_lambdaSum = 0;
	m_lambdaCount = 0;
	m_badLambdaCount = 0;
	m_rpmMin = 1e9f;
	m_rpmMax = 0;
	for (int i = 0; i < MAX_BINS; i++) {
		m_sum[i] = 0;
		m_count[i] = 0;
	}
	for (int i = 0; i < MS_NTONES; i++) {
		m_msYc[i] = 0;
		m_msYs[i] = 0;
		m_msUc[i] = 0;
		m_msUs[i] = 0;
	}
}

// Shared arming checks for both modes.  Returns true if it's safe to start.
bool WallFuelTuner::startCommon(float amplitude) {
	if (m_state != State::Idle) {
		efiPrintf("wwtune: already running, ignoring");
		return false;
	}

	if (!Sensor::hasSensor(SensorType::Lambda1)) {
		efiPrintf("wwtune: ABORT - no Lambda1 sensor configured");
		return false;
	}

	float rpm = Sensor::getOrZero(SensorType::Rpm);
	if (rpm < 400 || rpm > 4000) {
		efiPrintf("wwtune: ABORT - need steady running RPM in 400..4000 (got %.0f)", rpm);
		return false;
	}

	(void)amplitude;
	return true;
}

void WallFuelTuner::start(float amplitude, float halfPeriodMs, int periods) {
	// Apply defaults for zero/garbage arguments
	if (amplitude <= 0 || amplitude > 0.5f) {
		amplitude = 0.06f;
	}
	if (halfPeriodMs <= 0) {
		halfPeriodMs = 1500;
	}
	if (periods <= 0) {
		periods = 10;
	}

	if (!startCommon(amplitude)) {
		return;
	}

	int numBins = (int)(halfPeriodMs / BIN_WIDTH_MS);
	if (numBins > MAX_BINS) {
		numBins = MAX_BINS;
		// keep halfPeriod consistent with the bins we can actually store
		halfPeriodMs = numBins * BIN_WIDTH_MS;
	}
	if (numBins < 8) {
		efiPrintf("wwtune: ABORT - halfPeriod too short");
		return;
	}

	reset();

	m_mode = Mode::Step;
	m_amplitude = amplitude;
	m_halfPeriodMs = halfPeriodMs;
	m_periods = periods;
	m_numBins = numBins;
	m_settleHalfPeriods = 2;
	m_runTimer.reset();
	m_state = State::Collecting;

	efiPrintf(
			"wwtune: START step amp=%.0f%% halfPeriod=%.0fms periods=%d (~%.1fs). Hold throttle/RPM steady.",
			amplitude * 100,
			halfPeriodMs,
			periods,
			(2 * periods + m_settleHalfPeriods) * halfPeriodMs / 1000);
}

void WallFuelTuner::startMultisine(float amplitude, int periods) {
	if (amplitude <= 0 || amplitude > 0.5f) {
		amplitude = 0.06f;
	}
	if (periods <= 1) {
		periods = 4;
	}

	if (!startCommon(amplitude)) {
		return;
	}

	reset();

	// Tones are integer harmonics of f0 = 1/MS_BASE_PERIOD_S, chosen to be mutually
	// non-harmonic so intermodulation products land off the measured tones.
	static const int harmonics[MS_NTONES] = {1, 2, 3, 5, 8, 13, 21, 34};
	float f0 = 1 / MS_BASE_PERIOD_S;
	for (int k = 0; k < MS_NTONES; k++) {
		m_msFreq[k] = harmonics[k] * f0;
		// Quadratic (Schroeder-style) phase schedule to keep the crest factor low.
		m_msPhase[k] = CONST_PI * k * k / MS_NTONES;
	}

	// Scale the sum-of-sines so its peak excursion equals the requested amplitude,
	// keeping the perturbation within the linear range.  Evaluate over one base
	// period to find the true peak.
	float peak = 0;
	int steps = 2000;
	for (int i = 0; i <= steps; i++) {
		float t = MS_BASE_PERIOD_S * i / steps;
		float sum = 0;
		for (int k = 0; k < MS_NTONES; k++) {
			sum += sinf(2 * CONST_PI * m_msFreq[k] * t + m_msPhase[k]);
		}
		peak = std::max(peak, std::abs(sum));
	}
	m_msScale = (peak > 0.01f) ? (amplitude / peak) : 0;

	m_mode = Mode::Multisine;
	m_amplitude = amplitude;
	m_periods = periods;
	m_runTimer.reset();
	m_state = State::Collecting;

	efiPrintf(
			"wwtune: START multisine amp=%.0f%% tones=%d %.2f..%.2fHz periods=%d (~%.0fs). Hold warm & steady.",
			amplitude * 100,
			MS_NTONES,
			m_msFreq[0],
			m_msFreq[MS_NTONES - 1],
			periods,
			periods * MS_BASE_PERIOD_S);
}

void WallFuelTuner::accumulate(float lambda, int binIndex, int signOfEdge) {
	if (binIndex < 0 || binIndex >= m_numBins) {
		return;
	}
	m_sum[binIndex] += signOfEdge * lambda;
	m_count[binIndex]++;
}

void WallFuelTuner::onFastCallback() {
	if (m_state != State::Collecting) {
		m_fuelMult = 1;
		return;
	}

	float elapsedS = m_runTimer.getElapsedSeconds();

	if (m_mode == Mode::Step) {
		stepIteration(1000 * elapsedS);
	} else {
		multisineIteration(elapsedS);
	}
}

void WallFuelTuner::stepIteration(float elapsedMs) {
	int halfPeriodIndex = (int)(elapsedMs / m_halfPeriodMs);
	int totalHalfPeriods = m_settleHalfPeriods + 2 * m_periods;

	if (halfPeriodIndex >= totalHalfPeriods) {
		// done collecting, hand off to the fit (runs in slow callback)
		m_fuelMult = 1;
		m_state = State::Fit;
		return;
	}

	// Even half-period => fuel high (rising edge entering it), odd => fuel low.
	bool fuelHigh = (halfPeriodIndex % 2) == 0;
	m_fuelMult = 1 + (fuelHigh ? m_amplitude : -m_amplitude);

	// Skip the settle period - the first edge is only a half step from nominal.
	if (halfPeriodIndex < m_settleHalfPeriods) {
		return;
	}

	auto lambda = Sensor::get(SensorType::Lambda1);
	if (!lambda) {
		m_badLambdaCount++;
		return;
	}

	float rpm = Sensor::getOrZero(SensorType::Rpm);
	m_rpmMin = std::min(m_rpmMin, rpm);
	m_rpmMax = std::max(m_rpmMax, rpm);
	m_lambdaSum += lambda.Value;
	m_lambdaCount++;

	float msIntoHalfPeriod = elapsedMs - halfPeriodIndex * m_halfPeriodMs;
	int binIndex = (int)(msIntoHalfPeriod / BIN_WIDTH_MS);

	// Sign-correct so rising and falling fuel edges reinforce into one curve.
	int signOfEdge = fuelHigh ? +1 : -1;
	accumulate(lambda.Value, binIndex, signOfEdge);
}

void WallFuelTuner::multisineIteration(float elapsedS) {
	float totalS = m_periods * MS_BASE_PERIOD_S;

	if (elapsedS >= totalS) {
		m_fuelMult = 1;
		m_state = State::Fit;
		return;
	}

	// Generate the multisine fuel perturbation for this instant.
	float sum = 0;
	for (int k = 0; k < MS_NTONES; k++) {
		sum += sinf(2 * CONST_PI * m_msFreq[k] * elapsedS + m_msPhase[k]);
	}
	float u = m_msScale * sum;
	m_fuelMult = 1 + u;

	// Discard the first base period for settling.
	if (elapsedS < MS_BASE_PERIOD_S) {
		return;
	}

	auto lambda = Sensor::get(SensorType::Lambda1);
	if (!lambda) {
		m_badLambdaCount++;
		return;
	}

	float rpm = Sensor::getOrZero(SensorType::Rpm);
	m_rpmMin = std::min(m_rpmMin, rpm);
	m_rpmMax = std::max(m_rpmMax, rpm);
	m_lambdaSum += lambda.Value;
	m_lambdaCount++;

	// Quadrature-correlate both the known input u and the measured lambda against
	// each tone.  Forming H = Y/U later cancels the (identical) sample timing, so
	// callback jitter and the cos/sin convention don't matter.
	for (int k = 0; k < MS_NTONES; k++) {
		float theta = 2 * CONST_PI * m_msFreq[k] * elapsedS;
		float c = cosf(theta);
		float s = sinf(theta);
		m_msYc[k] += lambda.Value * c;
		m_msYs[k] += lambda.Value * s;
		m_msUc[k] += u * c;
		m_msUs[k] += u * s;
	}
}

void WallFuelTuner::onSlowCallback() {
	if (m_state == State::Fit) {
		if (m_mode == Mode::Step) {
			computeAndReport();
		} else {
			computeAndReportMultisine();
		}
		m_state = State::Idle;
	}
}

void WallFuelTuner::onEngineStop() {
	if (m_state != State::Idle) {
		efiPrintf("wwtune: aborted - engine stopped");
		reset();
	}
}

void WallFuelTuner::computeAndReport() {
	int n = m_numBins;

	// Build the signed, ensemble-averaged response curve.
	float raw[MAX_BINS];
	for (int b = 0; b < n; b++) {
		raw[b] = (m_count[b] > 0) ? (m_sum[b] / m_count[b]) : NAN;
	}
	// Backfill any empty bins (shouldn't normally happen)
	for (int b = 0; b < n; b++) {
		if (std::isnan(raw[b])) {
			raw[b] = (b > 0) ? raw[b - 1] : 0;
		}
	}

	// Baseline = average of the first 2 bins (still showing the previous level),
	// final = average of the last quarter (settled at the new level).
	float baseline = (raw[0] + raw[1]) / 2;
	int tailStart = n - std::max(1, n / 4);
	float finalVal = 0;
	int tailCount = 0;
	for (int b = tailStart; b < n; b++) {
		finalVal += raw[b];
		tailCount++;
	}
	finalVal /= tailCount;

	float swing = finalVal - baseline;

	float lambdaMean = (m_lambdaCount > 0) ? (m_lambdaSum / m_lambdaCount) : 0;
	float expectedSwing = lambdaMean * 2 * m_amplitude;
	float measuredGain = (expectedSwing > 1e-6f) ? (fabsf(swing) / expectedSwing) : 0;

	if (fabsf(swing) < 1e-4f || lambdaMean < 0.1f) {
		efiPrintf(
				"wwtune: FAIL - lambda response too small (swing=%.4f). "
				"Check wideband, increase amplitude, or hold a steadier point.",
				swing);
		return;
	}

	// Normalize to 0..1
	float s[MAX_BINS];
	for (int b = 0; b < n; b++) {
		s[b] = (raw[b] - baseline) / swing;
	}

	WallModelFit fit = fitWallModel(s, n, BIN_WIDTH_MS / 1000);

	efiPrintf("wwtune: ===== RESULT =====");
	efiPrintf("wwtune:   tau       = %.3f s   (-> wwaeTau)", fit.tau);
	efiPrintf("wwtune:   beta      = %.3f     (-> wwaeBeta)", fit.beta);
	efiPrintf("wwtune:   sensorTau = %.3f s   (lambda+exhaust lag, byproduct)", fit.sensorTau);
	efiPrintf("wwtune:   deadTime  = %.0f ms  (transport delay, byproduct)", fit.deadTimeSec * 1000);
	efiPrintf("wwtune:   gainCheck = %.2f     (DC gain, want ~1.0)", measuredGain);
	efiPrintf("wwtune:   fitRMS    = %.3f     (normalized residual, lower=better)", fit.rmsResidual);
	efiPrintf(
			"wwtune:   lambdaAvg = %.3f  rpm %.0f..%.0f  badLambda=%lu",
			lambdaMean,
			m_rpmMin,
			m_rpmMax,
			(unsigned long)m_badLambdaCount);

	if (m_rpmMax - m_rpmMin > 250) {
		efiPrintf("wwtune:   WARNING rpm moved %.0f - airflow not constant, result suspect", m_rpmMax - m_rpmMin);
	}

	// Dump the averaged step-response curve (normalized) for diagnostics.
	efiPrintf("wwtune: normalized response (per %.0fms bin):", BIN_WIDTH_MS);
	for (int b = 0; b < n; b += 8) {
		efiPrintf(
				"wwtune:   t=%4.0fms: %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f",
				b * BIN_WIDTH_MS,
				s[b],
				(b + 1 < n) ? s[b + 1] : 0,
				(b + 2 < n) ? s[b + 2] : 0,
				(b + 3 < n) ? s[b + 3] : 0,
				(b + 4 < n) ? s[b + 4] : 0,
				(b + 5 < n) ? s[b + 5] : 0,
				(b + 6 < n) ? s[b + 6] : 0,
				(b + 7 < n) ? s[b + 7] : 0);
	}
}

void WallFuelTuner::computeAndReportMultisine() {
	using cf = std::complex<float>;

	float lambdaMean = (m_lambdaCount > 0) ? (m_lambdaSum / m_lambdaCount) : 0;
	if (lambdaMean < 0.1f || m_msScale <= 0) {
		efiPrintf("wwtune: FAIL - no usable signal (lambdaAvg=%.3f). Check wideband.", lambdaMean);
		return;
	}

	// Build the measured complex response per tone.  Y and U use the e^{-j theta}
	// convention (real = sum*cos, imag = -sum*sin).  H_plant = -(Y/U)/lambdaMean:
	// the negation maps "lambda falls as fuel rises" to a +1 DC gain, and dividing
	// by lambdaMean normalizes lambda units to the dimensionless fuel fraction.
	float hReal[MS_NTONES];
	float hImag[MS_NTONES];
	float hMag[MS_NTONES];
	for (int k = 0; k < MS_NTONES; k++) {
		cf Y(m_msYc[k], -m_msYs[k]);
		cf U(m_msUc[k], -m_msUs[k]);
		cf H = -(Y / U) / lambdaMean;
		hReal[k] = std::real(H);
		hImag[k] = std::imag(H);
		hMag[k] = std::abs(H);
	}

	WallModelFit fit = fitWallModelFreq(m_msFreq, hReal, hImag, MS_NTONES);

	efiPrintf("wwtune: ===== RESULT (multisine) =====");
	efiPrintf("wwtune:   tau       = %.3f s   (-> wwaeTau)", fit.tau);
	efiPrintf("wwtune:   beta      = %.3f     (-> wwaeBeta)", fit.beta);
	efiPrintf("wwtune:   sensorTau = %.3f s   (lambda+exhaust lag, byproduct)", fit.sensorTau);
	efiPrintf("wwtune:   deadTime  = %.0f ms  (transport delay, byproduct)", fit.deadTimeSec * 1000);
	efiPrintf("wwtune:   gainLow   = %.2f     (|H| at %.2fHz, want ~1.0)", hMag[0], m_msFreq[0]);
	efiPrintf("wwtune:   fitRMS    = %.3f     (complex residual, lower=better)", fit.rmsResidual);
	efiPrintf(
			"wwtune:   lambdaAvg = %.3f  rpm %.0f..%.0f  badLambda=%lu",
			lambdaMean,
			m_rpmMin,
			m_rpmMax,
			(unsigned long)m_badLambdaCount);

	if (m_rpmMax - m_rpmMin > 250) {
		efiPrintf("wwtune:   WARNING rpm moved %.0f - airflow not constant, result suspect", m_rpmMax - m_rpmMin);
	}

	// Dump the measured Bode points for diagnostics.
	efiPrintf("wwtune: measured response (freq, |H|, phase):");
	for (int k = 0; k < MS_NTONES; k++) {
		float phaseDeg = atan2f(hImag[k], hReal[k]) * 180 / CONST_PI;
		efiPrintf("wwtune:   %5.2f Hz: |H|=%.3f  phase=%+.0f deg", m_msFreq[k], hMag[k], phaseDeg);
	}
}
