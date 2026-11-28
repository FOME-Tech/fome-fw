#include "pch.h"

#include "config_sequence.h"

#include <string>
#include <vector>

// Fake steps that record each call, and whether they got a previous configuration
static std::vector<std::string> calls;

static engine_configuration_s prevConfig;

static std::string describe(const char* what, const char* name, const engine_configuration_s* prev) {
	return std::string(what) + " " + name + (prev ? "" : " (scratch)");
}

#define FAKE_STEP(name)                                                                                                \
	static void stop_##name(const engine_configuration_s* prev) {                                                      \
		calls.push_back(describe("stop", #name, prev));                                                                \
	}                                                                                                                  \
	static void start_##name(const engine_configuration_s* prev) {                                                     \
		calls.push_back(describe("start", #name, prev));                                                               \
	}

FAKE_STEP(hw)
FAKE_STEP(a)
FAKE_STEP(b)
FAKE_STEP(reboot)

static bool rebootStepDiffers = false;

static bool rebootStepNeedsReboot() {
	return rebootStepDiffers;
}

static constexpr ConfigStep fakeSteps[] = {
		{"hw", BootPhase::Hardware, ApplyWhen::Immediately, stop_hw, start_hw, nullptr},
		{"reboot", BootPhase::Hardware, ApplyWhen::RebootOnly, stop_reboot, start_reboot, rebootStepNeedsReboot},
		{"a", BootPhase::Controller, ApplyWhen::Immediately, stop_a, start_a, nullptr},
		{"b", BootPhase::Controller, ApplyWhen::Immediately, stop_b, start_b, nullptr},
};

class ConfigSequenceTest : public ::testing::Test {
protected:
	void SetUp() override {
		calls.clear();
		rebootStepDiffers = false;
	}

	void boot() {
		dut.boot(BootPhase::Hardware);
		dut.boot(BootPhase::Controller);
		calls.clear();
	}

	void burn() {
		dut.beginChange(&prevConfig);
		calls.push_back("legacy");
		dut.finishChange(&prevConfig);
	}

	ConfigSequence dut{fakeSteps};
};

TEST_F(ConfigSequenceTest, BootStartsEachPhaseInOrder) {
	dut.boot(BootPhase::Hardware);
	EXPECT_EQ(calls, (std::vector<std::string>{"start hw (scratch)", "start reboot (scratch)"}));

	calls.clear();
	dut.boot(BootPhase::Controller);
	EXPECT_EQ(calls, (std::vector<std::string>{"start a (scratch)", "start b (scratch)"}));

	EXPECT_FALSE(dut.isRebootRequired());
}

TEST_F(ConfigSequenceTest, BurnStopsEverythingInReverseBeforeStartingInOrder) {
	boot();

	burn();

	// Reboot-only steps don't restart. Stops happen before whatever isn't in the sequence yet, starts after.
	EXPECT_EQ(
			calls,
			(std::vector<std::string>{"stop b", "stop a", "stop hw", "legacy", "start hw", "start a", "start b"}));
}

TEST_F(ConfigSequenceTest, RebootRequiredFollowsTheStep) {
	boot();

	rebootStepDiffers = true;
	burn();
	EXPECT_TRUE(dut.isRebootRequired());

	// Changed back to what it started with: no reboot needed after all
	rebootStepDiffers = false;
	burn();
	EXPECT_FALSE(dut.isRebootRequired());
}

TEST_F(ConfigSequenceTest, RebootClearsRebootRequired) {
	boot();

	rebootStepDiffers = true;
	burn();
	ASSERT_TRUE(dut.isRebootRequired());

	// A reboot starts everything again with the configuration that's now stored
	rebootStepDiffers = false;
	dut.boot(BootPhase::Hardware);
	dut.boot(BootPhase::Controller);

	EXPECT_FALSE(dut.isRebootRequired());
}
