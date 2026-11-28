/**
 * @file config_sequence.h
 *
 * Startup and configuration changes ("burn") run through the same ordered list of steps, so that a
 * setting written from TunerStudio takes effect the same way it would after a reboot with it.
 *
 * At startup each step's start() runs with prev == nullptr. On a burn, every step that can change has
 * stop() called, in reverse order, before any of them has start() called, in forward order: a pin released
 * by one step may be claimed by another. Order in the list is therefore dependency order.
 *
 * A burn applies at once, whether or not the engine is running. Steps keep that safe by touching only what
 * the change affects (see ConfigStep::stop), so a burn that doesn't change a setting leaves whatever
 * depends on it undisturbed. Changing something while the engine runs that it needs to run is the
 * operator's business.
 */

#pragma once

#include <cstddef>
#include <cstdint>

struct engine_configuration_s;

// When a step can apply a configuration change
enum class ApplyWhen : uint8_t {
	// As soon as it's burned
	Immediately,
	// Not reconfigurable at runtime. A change raises "reboot required" instead of taking effect.
	RebootOnly,
};

// Which part of startup a step first starts in
enum class BootPhase : uint8_t {
	// initHardware(): before the consoles, Lua and engine control start
	Hardware,
	// commonInitEngineController()
	Controller,
};

struct ConfigStep {
	const char* name;
	BootPhase phase;
	ApplyWhen when;

	// Optional. Release whatever the new configuration (engineConfiguration) no longer wants, and nothing
	// else: other threads keep using what stays. Never called at startup. prev is the configuration the
	// step last applied.
	void (*stop)(const engine_configuration_s* prev);

	// Apply the current configuration. prev == nullptr means start from scratch (startup). Otherwise prev
	// is the configuration the step last applied. Re-applying something that didn't change must not
	// disturb it.
	void (*start)(const engine_configuration_s* prev);

	// RebootOnly steps: whether the current configuration differs from what the step started with
	bool (*needsReboot)();
};

class ConfigSequence {
public:
	template <size_t N>
	constexpr ConfigSequence(const ConfigStep (&stepList)[N])
		: m_steps(stepList)
		, m_count(N) {}

	// Start every step of this phase, from scratch
	void boot(BootPhase phase);

	// A burn is beginChange(), the configuration changes that aren't in this sequence yet, then
	// finishChange(). prev is the configuration before the burn. Only once every phase has booted: the
	// caller makes sure of that (see configStepsStartupFinished()).
	void beginChange(const engine_configuration_s* prev);
	void finishChange(const engine_configuration_s* prev);

	// A RebootOnly step's configuration differs from what it started with
	bool isRebootRequired() const {
		return m_rebootRequired;
	}

private:
	// Whether a burn stops and starts this step
	static bool appliesChanges(const ConfigStep& step) {
		return step.when == ApplyWhen::Immediately;
	}

	void updateRebootRequired();

	const ConfigStep* const m_steps;
	const size_t m_count;

	bool m_rebootRequired = false;
};
