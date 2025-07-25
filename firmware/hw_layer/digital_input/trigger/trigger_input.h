/**
 * @file	trigger_input.h
 * @brief	Position sensor hardware layer
 *
 * @date Dec 30, 2012
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#pragma once

#include "trigger_structure.h"
#include "trigger_central.h"

void updateTriggerInputPins();

void onTriggerChanged(efitick_t stamp, bool isPrimary, bool isRising);
