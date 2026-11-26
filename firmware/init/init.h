/**
 * @file init.h
 */

#pragma once

// Call this once at startup for the sensors that don't depend on the configuration (built in to the
// board). The configured ones are started by the configuration sequence step below.
void initFixedSensors();

struct engine_configuration_s;

// Configuration sequence step, see config_steps.cpp: every sensor whose inputs come from the
// configuration, including the aux speed sensors and Lua digital inputs. Some sensors may not
// support every change and ignore it.
void stopConfiguredSensors(const engine_configuration_s* prev);
void startConfiguredSensors(const engine_configuration_s* prev);

// Internal init functions for individual systems
// Sensor init/config
void initVbatt();
void initMaf();
void initMap();
void initTps();
void initFluidPressure();
void initThermistors();
void initCanSensors();
void initLambda();
// Refresh each wideband's "time since last CAN frame" - call periodically, not on frame receipt
void updateWidebandAliveTimers();
void initFlexSensor();
void initFuelLevel();
void initBaro();
void initAuxSensors();
void initVehicleSpeedSensor();
void initTurbochargerSpeedSensor();
void initAuxSpeedSensors();

// Sensor reconfiguration
void deinitVbatt();
void deinitTps();
void deinitThermistors();
void deinitFluidPressure();
void deinitLambda();
void deInitFlexSensor();
void deinitAuxSensors();
void deInitVehicleSpeedSensor();
void deinitTurbochargerSpeedSensor();
void deinitMap();
void deinitMaf();
void deinitFuelLevel();
void deinitAuxSpeedSensors();
void deinitBaro();
