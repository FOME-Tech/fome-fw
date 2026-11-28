
#include "pch.h"

#include "gppwm_channel.h"

static GppwmChannel channels[GPPWM_CHANNELS];
static SimplePwm outputs[GPPWM_CHANNELS];
static ConfiguredPwm pwms[GPPWM_CHANNELS] = {
		ConfiguredPwm{outputs[0]},
		ConfiguredPwm{outputs[1]},
		ConfiguredPwm{outputs[2]},
		ConfiguredPwm{outputs[3]},
};

using gppwm_Map3D_t = Map3D<GPPWM_RPM_COUNT, GPPWM_LOAD_COUNT, uint8_t, int16_t, int16_t>;

static gppwm_Map3D_t table1;
static gppwm_Map3D_t table2;
static gppwm_Map3D_t table3;
static gppwm_Map3D_t table4;

static gppwm_Map3D_t* const tables[] = {
		&table1,
		&table2,
		&table3,
		&table4,
};

static const char* channelNames[GPPWM_CHANNELS] = {
		"GPPWM#1",
		"GPPWM#2",
		"GPPWM#3",
		"GPPWM#4",
};

// The pin itself is an EnginePins RegisteredOutputPin, claimed and moved by every burn

static bool isChannelWanted(size_t i) {
	return isBrainPinValid(engineConfiguration->gppwm[i].pin);
}

// A frequency of 0 means on-off mode
static bool isPwmWanted(size_t i) {
	return isChannelWanted(i) && engineConfiguration->gppwm[i].pwmFrequency >= 1;
}

void stopGpPwm(const engine_configuration_s* /*prev*/) {
	for (size_t i = 0; i < efi::size(channels); i++) {
		// Stop updating first: updateGppwm() runs on a higher priority thread, and could otherwise catch the
		// channel half reconfigured. The output holds its state until startGpPwm() attaches it again.
		channels[i].detach();

		pwms[i].stopIfUnwanted(isPwmWanted(i));
	}
}

void startGpPwm(const engine_configuration_s* prev) {
	for (size_t i = 0; i < efi::size(channels); i++) {
		if (!isChannelWanted(i)) {
			continue;
		}

		auto& cfg = engineConfiguration->gppwm[i];
		auto& pin = enginePins.gppwmPins[i];
		float frequency = cfg.pwmFrequency;

		pwms[i].startIfWanted(prev, isPwmWanted(i), channelNames[i], &pin, frequency, 0);

		if (pwms[i].isRunning()) {
			// Already running: follow a frequency change
			outputs[i].setFrequency(frequency);
		}

		// Set up this channel's lookup table
		tables[i]->init(cfg.table, cfg.loadBins, cfg.rpmBins);

		// Finally configure the channel
		channels[i].init(pwms[i].isRunning(), &outputs[i], &pin, tables[i], &cfg);
	}
}

void updateGppwm() {
	// There are only 8 debug float fields, this will overflow if more channels
	static_assert(efi::size(channels) <= 8);

	for (size_t i = 0; i < efi::size(channels); i++) {
		auto result = channels[i].update();

		engine->outputChannels.gppwmOutput[i] = result.Result;
		engine->outputChannels.gppwmXAxis[i] = result.X;
		engine->outputChannels.gppwmYAxis[i] = result.Y;
	}
}
