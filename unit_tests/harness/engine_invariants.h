/**
 * @file engine_invariants.h
 *
 * Checks of things that must always be true of a running engine, whatever the trigger pattern and
 * however the engine is moving. They work on what OutputRecorder and EngineSim recorded.
 *
 * Each returns a gtest AssertionResult describing the first few violations, so use them as
 *
 *   EXPECT_TRUE(noOverdwell(recorder, 10));
 *
 * Checks that take a time window only consider what happened at or after fromUs: use sim.nowUs()
 * to mark the point where a test stops setting up and starts caring.
 */

#pragma once

#include "harness/engine_sim.h"
#include "harness/output_recorder.h"

#include <gtest/gtest.h>

// ---- Outputs ----

// No coil (main or trailing) was ever charged for longer than maxDwellMs, including any still charging now
::testing::AssertionResult noOverdwell(const OutputRecorder& recorder, float maxDwellMs, int64_t fromUs = 0);

// No injector (either stage) was ever open for longer than maxOpenMs, including any still open now
::testing::AssertionResult noStuckInjector(const OutputRecorder& recorder, float maxOpenMs, int64_t fromUs = 0);

// Every coil charge period was between the two limits. Catches weak sparks as well as overdwell.
::testing::AssertionResult
dwellWithin(const OutputRecorder& recorder, float minDwellMs, float maxDwellMs, int64_t fromUs = 0);

// Every injector pulse (stage 1) was between the two limits
::testing::AssertionResult
injectionPulseWithin(const OutputRecorder& recorder, float minMs, float maxMs, int64_t fromUs = 0);

// No coil was fired without having been charged first
::testing::AssertionResult noSparkWithoutCharge(const OutputRecorder& recorder);

// Every output is inactive right now
::testing::AssertionResult allOutputsOff();

// Each of the first cylinderCount coils fired at the expected timing (degrees BTDC, as the firmware
// reports it), judged by where the crank really was. Sparks are compared modulo the sim's wheel
// period, so this checks timing accuracy, not whether the engine phase is right.
// Only meaningful for individual coils.
//
// periodDegrees is what sparks are compared modulo: leave it at 0 for the sim's wheel period. Pass
// something smaller when one coil serves several cylinders: 360 for wasted spark, or the firing
// interval for a single coil and distributor.
::testing::AssertionResult sparkTimingWithin(
		const OutputRecorder& recorder,
		const EngineSim& sim,
		float expectedAdvance,
		float toleranceDegrees,
		int64_t fromUs = 0,
		double periodDegrees = 0);

// Each of the first cylinderCount outputs of this kind was activated the same number of times, give or
// take one, with at least minPulses each. Catches a cylinder being skipped or doubled.
::testing::AssertionResult
evenPulseCount(const OutputRecorder& recorder, OutputKind kind, size_t minPulses, int64_t fromUs = 0);

// How long one output was active in each of count consecutive windows of windowUs, starting at fromUs.
// A pulse that straddles a window boundary is split between the two, so it doesn't matter where the
// windows fall. With a window of one engine cycle, this is the fuel each cycle got from an injector.
std::vector<float> activeTimePerWindow(
		const OutputRecorder& recorder, OutputKind kind, int index, int64_t fromUs, int64_t windowUs, size_t count);

// Each of the first cylinderCount injectors was open for between minMs and maxMs in every one of count
// consecutive windows of windowUs starting at fromUs
::testing::AssertionResult injectedPerWindowWithin(
		const OutputRecorder& recorder, int64_t fromUs, int64_t windowUs, size_t count, float minMs, float maxMs);

// ---- Decoder and RPM ----

// The decoder never reported an error
::testing::AssertionResult noTriggerErrors(const EngineSim& sim, int64_t fromUs = 0);

// The decoder was synchronized at every tooth
::testing::AssertionResult staysSynced(const EngineSim& sim, int64_t fromUs = 0);

// RPM never read zero while teeth were arriving
::testing::AssertionResult rpmNeverZero(const EngineSim& sim, int64_t fromUs = 0);

// Reported RPM stayed within a fraction of the true speed
::testing::AssertionResult rpmWithin(const EngineSim& sim, float toleranceFraction, int64_t fromUs = 0);

// The firmware never changed its mind about which revolution of the cycle the engine was on
::testing::AssertionResult phaseStable(const EngineSim& sim, int64_t fromUs = 0);
