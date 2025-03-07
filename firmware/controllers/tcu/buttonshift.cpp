/**
 * @file        buttonshift.cpp
 * @brief       Polls pins for gear changes
 *
 * @date Aug 31, 2020
 * @author David Holdeman, (c) 2020
 */

#include "pch.h"

#include "buttonshift.h"

#if EFI_TCU
ButtonShiftController buttonShiftController;

ButtonShiftController::ButtonShiftController() :
		debounceUp("gear_up"),
		debounceDown("gear_down")
		{

}

void ButtonShiftController::init() {
	// 500 millisecond is maybe a little long?
	debounceUp.init(MS2NT(500), engineConfiguration->tcuUpshiftButtonPin, engineConfiguration->tcuUpshiftButtonPinMode);
	debounceDown.init(MS2NT(500), engineConfiguration->tcuDownshiftButtonPin, engineConfiguration->tcuDownshiftButtonPinMode);

	GearControllerBase::init();
}

void ButtonShiftController::update() {
    bool upPinState = false;
    bool downPinState = false;
    // Read pins
    upPinState = debounceUp.readPinEvent();
    downPinState = debounceDown.readPinEvent();
    gear_e gear = getDesiredGear();
    // Select new gear based on current desired gear.
    if (upPinState) {
        switch (gear) {
            case REVERSE:
                setDesiredGear(NEUTRAL);
                break;
            case NEUTRAL:
                setDesiredGear(GEAR_1);
                break;
            case GEAR_1:
                setDesiredGear(GEAR_2);
                break;
            case GEAR_2:
                setDesiredGear(GEAR_3);
                break;
            case GEAR_3:
                setDesiredGear(GEAR_4);
                break;
            default:
                break;
        }
    } else if (downPinState) {
        switch (gear) {
            case NEUTRAL:
                setDesiredGear(REVERSE);
                break;
            case GEAR_1:
                setDesiredGear(NEUTRAL);
                break;
            case GEAR_2:
                setDesiredGear(GEAR_1);
                break;
            case GEAR_3:
                setDesiredGear(GEAR_2);
                break;
            case GEAR_4:
                setDesiredGear(GEAR_3);
                break;
            default:
                break;
        }
    }

	GearControllerBase::update();
}

ButtonShiftController* getButtonShiftController() {
	return &buttonShiftController;
}
#endif // EFI_TCU
