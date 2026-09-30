/**
 * @file    flash_main.h
 * @brief
 *
 * @date Sep 19, 2013
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#pragma once

void readFromFlash();
void initFlash();

/**
 * Scheduled writes on STM32F4/F7 wait until the engine stops because internal
 * flash operations can stall execution. Direct callers must ensure the engine
 * is stopped. All blocking calibration writes inhibit the ETB/DC controllers
 * until fresh sensor samples and a new control cycle permit recovery.
 */
void writeToFlashNow();
void setNeedToWriteConfiguration();
/**
 * @return true if a flash write is pending or the last attempt failed.
 * A failed attempt requires a new request before another automatic write.
 */
bool getNeedToWriteConfiguration();
void writeToFlashIfPending();
