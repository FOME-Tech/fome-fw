/**
 * @file config_steps.h
 *
 * The firmware's configuration sequence, see config_sequence.h. The steps themselves are listed in
 * config_steps.cpp, in dependency order.
 */

#pragma once

#include "config_sequence.h"

// Start this phase's steps. Called once each, at the point in startup the phase belongs to, while
// startup holds ConfigChangeLock.
void configStepsBoot(BootPhase phase);

// Startup has finished and engine control runs: from now on a burn applies. Called by startup before it
// lets go of ConfigChangeLock, so a burn that arrived earlier waits for it, then applies to a fully
// started ECU. If startup stops short (validateConfig() rejects the configuration), it's never called,
// and burns are only saved, to apply at the next reboot.
void configStepsStartupFinished();
bool isStartupFinished();

#if EFI_UNIT_TEST
// Each test starts the firmware again from nothing
void configStepsReset();
#endif

// Burn, see ConfigSequence. prev is the configuration before the burn. Only after startup has finished.
void configStepsBeginChange(const engine_configuration_s* prev);
void configStepsFinishChange(const engine_configuration_s* prev);

bool isConfigRebootRequired();

// Held while the configuration is written or applied: by startup (from before the consoles start until
// startup has finished), a burn and a TunerStudio write. Burns can come from TunerStudio, the console and
// Lua, each on its own thread. Not recursive.
class ConfigChangeLock {
public:
	ConfigChangeLock();
	~ConfigChangeLock();
};
