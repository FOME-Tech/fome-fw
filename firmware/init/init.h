/**
 * @file init.h
 */

#pragma once

// Call this once at startup to initialize, configure, and subscribe sensors
void initNewSensors();

void stopSensors();

// Call this whenever the configuration may have changed, so any sensors
// can be reconfigured with the new settings.
// Note: this may not be necessarily possible for all sensors, so some may
// do nothing when this is called.
void reconfigureSensors();

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
void deinitAuxSpeedSensors();
