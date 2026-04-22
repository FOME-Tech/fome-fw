/**
 * @file test_monotonic_angle.cpp
 *
 * Tests for the MonotonicAngle type used for unambiguous event scheduling.
 */

#include "engine_phase_angle.h"
#include "pch.h"

// Test basic construction and reset
TEST(MonotonicAngle, BasicConstruction) {
	MonotonicAngle angle;
	EXPECT_EQ(0, angle.cycleCount);
	EXPECT_FLOAT_EQ(0, angle.intraCycleAngle);
}

TEST(MonotonicAngle, Reset) {
	MonotonicAngle angle;
	angle.cycleCount = 100;
	angle.intraCycleAngle = 123.45f;

	angle.reset();

	EXPECT_EQ(0, angle.cycleCount);
	EXPECT_FLOAT_EQ(0, angle.intraCycleAngle);
}

// Test advance without cycle rollover
TEST(MonotonicAngle, AdvanceWithinCycle) {
	MonotonicAngle angle;
	const float engineCycle = 720.0f; // 4-stroke

	angle.advance(100.0f, engineCycle);
	EXPECT_EQ(0, angle.cycleCount);
	EXPECT_FLOAT_EQ(100.0f, angle.intraCycleAngle);

	angle.advance(200.0f, engineCycle);
	EXPECT_EQ(0, angle.cycleCount);
	EXPECT_FLOAT_EQ(300.0f, angle.intraCycleAngle);
}

// Test advance with cycle rollover
TEST(MonotonicAngle, AdvanceWithCycleRollover) {
	MonotonicAngle angle;
	const float engineCycle = 720.0f;

	// Start at 600 degrees
	angle.intraCycleAngle = 600.0f;

	// Advance by 200 degrees - should rollover
	angle.advance(200.0f, engineCycle);

	EXPECT_EQ(1, angle.cycleCount);
	EXPECT_FLOAT_EQ(80.0f, angle.intraCycleAngle);
}

// Test advance with multiple cycle rollovers
TEST(MonotonicAngle, AdvanceMultipleCycleRollovers) {
	MonotonicAngle angle;
	const float engineCycle = 720.0f;

	// Advance by more than two full cycles
	angle.advance(1600.0f, engineCycle);

	EXPECT_EQ(2, angle.cycleCount);
	EXPECT_FLOAT_EQ(160.0f, angle.intraCycleAngle);
}

// Test comparison operators - same cycle
TEST(MonotonicAngle, ComparisonSameCycle) {
	MonotonicAngle a, b;

	a.cycleCount = 5;
	a.intraCycleAngle = 100.0f;

	b.cycleCount = 5;
	b.intraCycleAngle = 200.0f;

	EXPECT_TRUE(a < b);
	EXPECT_FALSE(b < a);
	EXPECT_TRUE(b > a);
	EXPECT_FALSE(a > b);
	EXPECT_TRUE(a <= b);
	EXPECT_TRUE(b >= a);
	EXPECT_FALSE(a == b);
	EXPECT_TRUE(a != b);
}

// Test comparison operators - different cycles
TEST(MonotonicAngle, ComparisonDifferentCycles) {
	MonotonicAngle a, b;

	// a is in cycle 5 with high angle
	a.cycleCount = 5;
	a.intraCycleAngle = 700.0f;

	// b is in cycle 6 with low angle
	b.cycleCount = 6;
	b.intraCycleAngle = 10.0f;

	// b should be greater because it's in a later cycle
	EXPECT_TRUE(a < b);
	EXPECT_FALSE(b < a);
	EXPECT_TRUE(b > a);
	EXPECT_FALSE(a > b);
}

// Test equality
TEST(MonotonicAngle, Equality) {
	MonotonicAngle a, b;

	a.cycleCount = 10;
	a.intraCycleAngle = 360.0f;

	b.cycleCount = 10;
	b.intraCycleAngle = 360.0f;

	EXPECT_TRUE(a == b);
	EXPECT_FALSE(a != b);
	EXPECT_FALSE(a < b);
	EXPECT_FALSE(a > b);
	EXPECT_TRUE(a <= b);
	EXPECT_TRUE(a >= b);
}

// Test that monotonicity is preserved across many advances
TEST(MonotonicAngle, MonotonicityAcrossAdvances) {
	MonotonicAngle angle;
	MonotonicAngle prevAngle;
	const float engineCycle = 720.0f;

	// Simulate many tooth events with varying deltas
	float deltas[] = {10.0f, 15.0f, 30.0f, 60.0f, 90.0f, 120.0f};

	for (int i = 0; i < 1000; i++) {
		float delta = deltas[i % 6];
		prevAngle = angle;
		angle.advance(delta, engineCycle);

		// After each advance, the angle should be strictly greater
		EXPECT_TRUE(angle > prevAngle) << "Monotonicity violated at iteration " << i;
	}
}

// Test two-stroke engine cycle (360 degrees)
TEST(MonotonicAngle, TwoStrokeCycle) {
	MonotonicAngle angle;
	const float engineCycle = 360.0f; // 2-stroke

	angle.advance(400.0f, engineCycle);

	EXPECT_EQ(1, angle.cycleCount);
	EXPECT_FLOAT_EQ(40.0f, angle.intraCycleAngle);
}

// Test with very small deltas (high-tooth-count wheel)
TEST(MonotonicAngle, SmallDeltas) {
	MonotonicAngle angle;
	const float engineCycle = 720.0f;

	// 60-2 tooth wheel: about 6 degrees per tooth
	for (int i = 0; i < 58; i++) {
		angle.advance(6.0f, engineCycle);
	}
	// After 58 teeth, we've advanced 348 degrees
	EXPECT_EQ(0, angle.cycleCount);
	EXPECT_NEAR(348.0f, angle.intraCycleAngle, 0.001f);

	// Complete a full two revolutions (60 teeth * 2 = 120 teeth, minus 4 missing)
	// That's 116 more teeth at 6 deg each = 696 degrees
	for (int i = 0; i < 116; i++) {
		angle.advance(6.0f, engineCycle);
	}
	// Total: 348 + 696 = 1044 degrees = 1 full cycle + 324 degrees
	EXPECT_EQ(1, angle.cycleCount);
	EXPECT_NEAR(324.0f, angle.intraCycleAngle, 0.01f);
}

// Test edge case: advance by exactly one cycle
TEST(MonotonicAngle, AdvanceExactlyOneCycle) {
	MonotonicAngle angle;
	const float engineCycle = 720.0f;

	angle.intraCycleAngle = 100.0f;
	angle.advance(720.0f, engineCycle);

	EXPECT_EQ(1, angle.cycleCount);
	EXPECT_FLOAT_EQ(100.0f, angle.intraCycleAngle);
}

// Test edge case: advance to exactly end of cycle
TEST(MonotonicAngle, AdvanceToExactCycleBoundary) {
	MonotonicAngle angle;
	const float engineCycle = 720.0f;

	angle.intraCycleAngle = 360.0f;
	angle.advance(360.0f, engineCycle);

	// Should roll over to next cycle at 0 degrees
	EXPECT_EQ(1, angle.cycleCount);
	EXPECT_FLOAT_EQ(0.0f, angle.intraCycleAngle);
}

// ============ Tests for update() method (authoritative angle, no accumulation)
// ============

// Test basic update without rollover
TEST(MonotonicAngle, UpdateWithinCycle) {
	MonotonicAngle angle;
	const float engineCycle = 720.0f;

	// Simulate tooth at 100 degrees
	angle.update(100.0f, 100.0f, engineCycle);
	EXPECT_EQ(0, angle.cycleCount);
	EXPECT_FLOAT_EQ(100.0f, angle.intraCycleAngle);

	// Next tooth at 200 degrees (delta = 100)
	angle.update(200.0f, 100.0f, engineCycle);
	EXPECT_EQ(0, angle.cycleCount);
	EXPECT_FLOAT_EQ(200.0f, angle.intraCycleAngle);
}

// Test update with cycle rollover
TEST(MonotonicAngle, UpdateWithCycleRollover) {
	MonotonicAngle angle;
	const float engineCycle = 720.0f;

	// Start at 700 degrees
	angle.update(700.0f, 700.0f, engineCycle);
	EXPECT_EQ(0, angle.cycleCount);
	EXPECT_FLOAT_EQ(700.0f, angle.intraCycleAngle);

	// Next tooth at 10 degrees (wrapped), delta = 10 - 700 = -690 (negative means
	// rollover)
	angle.update(10.0f, -690.0f, engineCycle);
	EXPECT_EQ(1, angle.cycleCount);
	EXPECT_FLOAT_EQ(10.0f, angle.intraCycleAngle);
}

// Test that update uses authoritative angle exactly (no accumulation drift)
TEST(MonotonicAngle, UpdateNoAccumulationDrift) {
	MonotonicAngle angle;
	const float engineCycle = 720.0f;

	// Simulate 10000 teeth with small deltas that could accumulate error
	float currentAngle = 0.0f;
	float previousAngle = 0.0f;
	const float smallDelta = 6.0f; // Like a 60-tooth wheel

	for (int i = 0; i < 10000; i++) {
		previousAngle = currentAngle;
		currentAngle += smallDelta;
		if (currentAngle >= engineCycle) {
			currentAngle -= engineCycle;
		}

		float rawDelta = currentAngle - previousAngle;
		angle.update(currentAngle, rawDelta, engineCycle);

		// The intraCycleAngle should EXACTLY match the authoritative angle
		// (no floating-point accumulation drift)
		EXPECT_FLOAT_EQ(currentAngle, angle.intraCycleAngle) << "Drift detected at iteration " << i;
	}

	// After 10000 teeth at 6 degrees each = 60000 degrees = 83 full cycles + 240
	// degrees 60000 / 720 = 83.333..., 60000 - (83 * 720) = 60000 - 59760 = 240
	EXPECT_EQ(83, angle.cycleCount);
	EXPECT_NEAR(240.0f, angle.intraCycleAngle, 0.1f);
}

// Test that update doesn't detect rollover for large positive delta (backward
// motion indicator)
TEST(MonotonicAngle, UpdateLargeDeltaNoFalseRollover) {
	MonotonicAngle angle;
	const float engineCycle = 720.0f;

	angle.cycleCount = 5;
	angle.intraCycleAngle = 100.0f;

	// Large positive delta > engineCycle/2 indicates anomaly, not rollover
	angle.update(500.0f, 400.0f, engineCycle);

	// Should NOT increment cycle count
	EXPECT_EQ(5, angle.cycleCount);
	EXPECT_FLOAT_EQ(500.0f, angle.intraCycleAngle);
}

// Test update monotonicity across many cycles
TEST(MonotonicAngle, UpdateMonotonicityAcrossCycles) {
	MonotonicAngle angle;
	MonotonicAngle prevAngle;
	const float engineCycle = 720.0f;

	float currentAngle = 0.0f;
	float previousAngle = 0.0f;
	float delta = 15.0f; // Simulating 48-tooth wheel

	for (int i = 0; i < 5000; i++) {
		previousAngle = currentAngle;
		currentAngle += delta;
		if (currentAngle >= engineCycle) {
			currentAngle -= engineCycle;
		}

		prevAngle = angle;
		float rawDelta = currentAngle - previousAngle;
		angle.update(currentAngle, rawDelta, engineCycle);

		// Monotonicity: each update should result in strictly greater angle
		EXPECT_TRUE(angle > prevAngle) << "Monotonicity violated at iteration " << i;
	}
}
