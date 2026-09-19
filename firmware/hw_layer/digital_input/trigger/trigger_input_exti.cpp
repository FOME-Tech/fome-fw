/**
 * @file	trigger_input_exti.cpp
 * @brief	Position sensor hardware layer - PAL version
 *
 * todo: VVT implementation is a nasty copy-paste :(
 *
 * see digital_input_icu.cp
 *
 * @date Dec 30, 2012
 * @author Andrey Belomutskiy, (c) 2012-2021
 */

#include "pch.h"

#if EFI_SHAFT_POSITION_INPUT && EFI_PROD_CODE

#include "trigger_input.h"
#include "digital_input_exti.h"

#if (PAL_USE_CALLBACKS == FALSE)
#error "PAL_USE_CALLBACKS should be enabled to use HAL_TRIGGER_USE_PAL"
#endif

static void shaft_callback(void* arg, efitick_t stamp, bool rise) {
	int index = (int)arg;

	// todo: support for 3rd trigger input channel
	// todo: start using real event time from HW event, not just software timer?

	hwHandleShaftSignal(index, rise, stamp);
}

static void cam_callback(void* arg, efitick_t stamp, bool rise) {
	int index = (int)arg;

	hwHandleVvtCamSignal(rise, stamp, index);
}

/*==========================================================================*/
/* Exported functions.														*/
/*==========================================================================*/

int extiTriggerTurnOnInputPin(const char* msg, int index, bool isTriggerShaft) {
	brain_pin_e brainPin =
			isTriggerShaft ? engineConfiguration->triggerInputPins[index] : engineConfiguration->camInputs[index];

	efiPrintf("Trigger input (EXTI) for \"%s\" on \"%s\"", msg, hwPortname(brainPin));

	/* TODO:
	 * * do not set to both edges if we need only one
	 * * simplify callback in case of one edge */
	efiExtiEnablePin(
			msg, brainPin, PAL_EVENT_MODE_BOTH_EDGES, isTriggerShaft ? shaft_callback : cam_callback, (void*)index);

	return 0;
}

void extiTriggerTurnOffInputPin(brain_pin_e brainPin) {
	efiExtiDisablePin(brainPin);
}

#endif /* EFI_SHAFT_POSITION_INPUT && EFI_PROD_CODE */
