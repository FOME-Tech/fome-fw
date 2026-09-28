#pragma once

// Filtering is covered by test_biquad.cpp; this fixture observes buffer ownership.
struct Biquad {
	void configureBandpass(float, float, float) {}
	void cookSteadyState(float) {}
	float filter(float value) {
		return value;
	}
};
