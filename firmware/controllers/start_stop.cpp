#include "pch.h"

#include "start_stop.h"

ButtonDebounce startStopButtonDebounce("start_button");

void startStartStopButton(const engine_configuration_s* prev) {
	/* startCrankingDuration is efitimesec_t, so we need to multiply it by 1000 to get milliseconds*/
	efidur_t threshold = MS2NT(engineConfiguration->startCrankingDuration * 1000);

	if (!prev) {
		startStopButtonDebounce.init(
				threshold, engineConfiguration->startStopButtonPin, engineConfiguration->startStopButtonMode);
	} else {
		// The pin follows the configuration through ButtonDebounce::startConfigurationList() on a burn.
		// Only the debounce time needs updating, without resetting the debounce state.
		startStopButtonDebounce.setThreshold(threshold);
	}
}
