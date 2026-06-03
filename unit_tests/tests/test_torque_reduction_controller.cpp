#include "pch.h"

#include "torque_reduction_controller.h"

namespace {
// Retard table axis spans requests 0..0.7 and ramps to 14 deg. Beyond 0.7 the request runs
// off the end of the table, so retard holds at 14 deg and cut takes over, ramping to a full
// cut at a request of 1.
void setupCalibration() {
	copyArray(config->torqueReductionRetardReqBins, {0.0f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f});
	copyArray(config->torqueReductionRetard, {0, 2, 4, 6, 8, 10, 12, 14});
}
} // namespace

TEST(TorqueReduction, RetardOnlyWithinTableAxis) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setupCalibration();

	auto& dut = engine->torqueReductionController;

	// No reduction requested: no retard, no cut
	{
		auto out = dut.getReduction(0);
		EXPECT_NEAR(out.retardDeg, 0, 0.1);
		EXPECT_NEAR(out.cutFraction, 0, 0.001);
	}

	// Within the table axis: retard from the table, still no cut
	{
		auto out = dut.getReduction(0.4f);
		EXPECT_NEAR(out.retardDeg, 8, 0.1);
		EXPECT_NEAR(out.cutFraction, 0, 0.001);
	}

	// At the end of the table axis: retard pinned at max, cut still zero
	{
		auto out = dut.getReduction(0.7f);
		EXPECT_NEAR(out.retardDeg, 14, 0.1);
		EXPECT_NEAR(out.cutFraction, 0, 0.001);
	}
}

TEST(TorqueReduction, CutSupplementsBeyondTableAxis) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setupCalibration();

	auto& dut = engine->torqueReductionController;

	// Halfway past the end of the axis: retard held at max, cut remaps to 0.5
	{
		auto out = dut.getReduction(0.85f);
		EXPECT_NEAR(out.retardDeg, 14, 0.1);
		EXPECT_NEAR(out.cutFraction, 0.5f, 0.001);
	}

	// Full reduction: max retard plus full cut
	{
		auto out = dut.getReduction(1.0f);
		EXPECT_NEAR(out.retardDeg, 14, 0.1);
		EXPECT_NEAR(out.cutFraction, 1.0f, 0.001);
	}
}

TEST(TorqueReduction, ClampsOutOfRangeRequest) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setupCalibration();

	auto& dut = engine->torqueReductionController;

	auto below = dut.getReduction(-0.5f);
	EXPECT_NEAR(below.retardDeg, 0, 0.1);
	EXPECT_NEAR(below.cutFraction, 0, 0.001);

	auto above = dut.getReduction(2.0f);
	EXPECT_NEAR(above.retardDeg, 14, 0.1);
	EXPECT_NEAR(above.cutFraction, 1.0f, 0.001);
}

TEST(TorqueReduction, TableAxisToFullRequestNeverCuts) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	// When the table axis already reaches a request of 1, there is no room left for cut, so
	// it is retard-only - not even a full reduction request produces a cut.
	copyArray(config->torqueReductionRetardReqBins, {0.0f, 0.15f, 0.3f, 0.45f, 0.6f, 0.75f, 0.9f, 1.0f});
	copyArray(config->torqueReductionRetard, {0, 2, 4, 6, 8, 10, 12, 14});

	auto& dut = engine->torqueReductionController;

	auto out = dut.getReduction(1.0f);
	EXPECT_NEAR(out.retardDeg, 14, 0.1);
	EXPECT_NEAR(out.cutFraction, 0, 0.001);
}

TEST(TorqueReduction, GatedOffWhenDisabled) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setupCalibration();

	auto& dut = engine->torqueReductionController;
	dut.setReductionRequest(1.0f);

	// Disabled: request is ignored, no retard or cut
	engineConfiguration->torqueReductionEnabled = false;
	EXPECT_NEAR(dut.update(), 0, 0.1);
	EXPECT_NEAR(dut.reductionRequest, 0, 0.001);
	EXPECT_NEAR(dut.cutFraction, 0, 0.001);

	// Enabled: the stored request drives retard + cut, and state is published (request/cut log in percent)
	engineConfiguration->torqueReductionEnabled = true;
	EXPECT_NEAR(dut.update(), 14, 0.1);
	EXPECT_NEAR(dut.reductionRequest, 100, 0.5);
	EXPECT_NEAR(dut.retardApplied, 14, 0.1);
	EXPECT_NEAR(dut.cutFraction, 100, 0.5);
}
