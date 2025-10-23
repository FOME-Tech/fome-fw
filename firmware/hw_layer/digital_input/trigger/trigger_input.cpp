/*
 * @file	trigger_input.cpp
 *
 * @date Nov 11, 2019
 * @author Andrey Belomutskiy, (c) 2012-2021
 */

#include "pch.h"
#include "trigger_input.h"

/* TODO:
 * - merge comparator trigger
 */

#if (EFI_SHAFT_POSITION_INPUT) || defined(__DOXYGEN__)

#if (HAL_TRIGGER_USE_PAL == TRUE)

int  extiTriggerTurnOnInputPin(const char *msg, int index, bool isTriggerShaft);
void extiTriggerTurnOffInputPin(brain_pin_e brainPin);

enum triggerType {
	TRIGGER_NONE,
	TRIGGER_EXTI,
};

static triggerType shaftTriggerType[TRIGGER_INPUT_PIN_COUNT];
static triggerType camTriggerType[CAM_INPUTS_COUNT];

static int turnOnTriggerInputPin(const char *msg, int index, bool isTriggerShaft) {
	brain_pin_e brainPin = isTriggerShaft ?
		engineConfiguration->triggerInputPins[index] : engineConfiguration->camInputs[index];

	if (isTriggerShaft) {
		shaftTriggerType[index] = TRIGGER_NONE;
	} else {
		camTriggerType[index] = TRIGGER_NONE;
	}

	if (!isBrainPinValid(brainPin)) {
		return 0;
	}

	/* ... then EXTI */
	if (extiTriggerTurnOnInputPin(msg, index, isTriggerShaft) >= 0) {
		if (isTriggerShaft) {
			shaftTriggerType[index] = TRIGGER_EXTI;
		} else {
			camTriggerType[index] = TRIGGER_EXTI;
		}
		return 0;
	}

	firmwareError(ObdCode::CUSTOM_ERR_NOT_INPUT_PIN, "%s: Not input pin %s", msg, hwPortname(brainPin));

	return -1;
}

static void turnOffTriggerInputPin(int index, bool isTriggerShaft) {
	brain_pin_e brainPin = isTriggerShaft ?
		activeConfiguration.triggerInputPins[index] : activeConfiguration.camInputs[index];

	if (isTriggerShaft) {
		if (shaftTriggerType[index] == TRIGGER_EXTI) {
			extiTriggerTurnOffInputPin(brainPin);
		}

		shaftTriggerType[index] = TRIGGER_NONE;
	} else {
		if (camTriggerType[index] == TRIGGER_EXTI) {
			extiTriggerTurnOffInputPin(brainPin);
		}

		camTriggerType[index] = TRIGGER_NONE;
	}
}

static void stopTriggerInputPins() {
	for (int i = 0; i < TRIGGER_INPUT_PIN_COUNT; i++) {
		if (isConfigurationChanged(triggerInputPins[i])) {
			turnOffTriggerInputPin(i, true);
		}
	}
	for (int i = 0; i < CAM_INPUTS_COUNT; i++) {
		if (isConfigurationChanged(camInputs[i])) {
			turnOffTriggerInputPin(i, false);
		}
	}
}

static const char* const camNames[] = { "Cam B1I", "Cam B1E", "Cam B2I", "Cam B2E"};

static void startTriggerInputPins() {
	for (int i = 0; i < TRIGGER_INPUT_PIN_COUNT; i++) {
		if (isConfigurationChanged(triggerInputPins[i])) {
			const char * msg = (i == 0 ? "Trigger #1" : (i == 1 ? "Trigger #2" : "Trigger #3"));
			turnOnTriggerInputPin(msg, i, true);
		}
	}

	for (int i = 0; i < CAM_INPUTS_COUNT; i++) {
		if (isConfigurationChanged(camInputs[i])) {
			turnOnTriggerInputPin(camNames[i], i, false);
		}
	}
}

#endif /* (HAL_TRIGGER_USE_PAL == TRUE) */

void updateTriggerInputPins() {
	if (hasFirmwareError()) {
		return;
	}

#if EFI_PROD_CODE
#if HAL_TRIGGER_USE_PAL
	// first we will turn off all the changed pins
	stopTriggerInputPins();
#endif // HAL_TRIGGER_USE_PAL

	if (isBrainPinValid(engineConfiguration->triggerInputPins[0])) {
		engine->rpmCalculator.Register();
	} else {
		// if we do not have primary input channel maybe it's BCM mode and we inject RPM value via Lua?
		engine->rpmCalculator.unregister();
	}

#if HAL_TRIGGER_USE_PAL
	// then we will enable all the changed pins
	startTriggerInputPins();
#endif // HAL_TRIGGER_USE_PAL
#endif /* EFI_PROD_CODE */
}

#endif /* EFI_SHAFT_POSITION_INPUT */
