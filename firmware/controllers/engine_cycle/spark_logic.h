/*
 * @file spark_logic.h
 *
 * @date Sep 15, 2016
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#pragma once

void onTriggerEventSparkLogic(efitick_t edgeTimestamp, float currentPhase, float nextPhase);
void turnSparkPinHigh(IgnitionEvent *event);
void fireSparkAndPrepareNextSchedule(IgnitionEvent *event);
int getNumberOfSparks(ignition_mode_e mode);
percent_t getCoilDutyCycle(float rpm);
void initializeIgnitionActions();
