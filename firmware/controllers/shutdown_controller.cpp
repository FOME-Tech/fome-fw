/*
 * @file shutdown_controller.cpp
 *
 */

#include "pch.h"

void doScheduleStopEngine() {
	efiPrintf("Starting doScheduleStopEngine");
	getLimpManager()->shutdownController.stopEngine();
	// todo: initiate stepper motor parking
}
