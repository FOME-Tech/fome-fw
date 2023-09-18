/**
 * @file    sensor_type.h
 * @brief   Enumeration of sensors supported by the ECU.
 *
 * @date September 12, 2019
 * @author Matthew Kennedy, (c) 2019
 */

#pragma once

#define VBAT_FALLBACK_VALUE 12

/**
 **************************************
 * SEE sensor.h ON HOW TO ADD NEW SENSOR TYPES
 **************************************
 */
enum class SensorType : unsigned char {
	Invalid, // we need indeces for Lua consumers. At the moment we still do not expose constants into Lua :(
	Clt, // 1
	Iat,
	Rpm,
	/**
	 * This value is result of averaging within user-defined window
	 * See also MapFast, MapSlow
	 */
	Map,
	Maf,

	AmbientTemperature,

	OilPressure,
	OilTemperature,

	FuelPressureLow, // in kPa
	FuelPressureHigh, // in kPa
	FuelPressureInjector,

	FuelTemperature,

	// This is the "resolved" position, potentially composited out of the following two
	Tps1, // 10
	// This is the first sensor
	Tps1Primary,
	// This is the second sensor
	Tps1Secondary,

	Tps2,
	Tps2Primary,
	Tps2Secondary,

	// Redundant and combined sensors for acc pedal
	AcceleratorPedal,
	AcceleratorPedalPrimary,
	AcceleratorPedalSecondary,

	// This maps to the pedal if we have one, and Tps1 if not.
	DriverThrottleIntent,

	AuxTemp1, // 20
	AuxTemp2,

	Lambda1,
	Lambda2,

	WastegatePosition,

	FuelEthanolPercent,

	BatteryVoltage,

	BarometricPressure,

	FuelLevel,

	VehicleSpeed,

	TurbochargerSpeed,

	// Fast MAP is synchronous to crank angle - user selectable phase/window
	MapFast,
	// Slow MAP is asynchronous - not synced to anything, normal analog sampling
	// MAP decoding happens only that often thus this is NOT raw MAP as flows from ADC
	MapSlow,

	InputShaftSpeed,

	EGT1,
	EGT2,

	Maf2,	// Second bank MAF sensor

	Map2,
	MapSlow2,
	MapFast2,

	// Pressure sensor after compressor, before intercooler
	CompressorDischargePressure,
	CompressorDischargeTemperature,

	// Pressure sensor before the throttle, after any turbo/etc
	ThrottleInletPressure,

	DetectedGear,

	// analog voltage inputs for Lua
	AuxAnalog1,
	AuxAnalog2,
	AuxAnalog3,
	AuxAnalog4,
	AuxAnalog5,
	AuxAnalog6,
	AuxAnalog7,
	AuxAnalog8,

	LuaGauge1,
	LuaGauge2,

	AuxLinear1,
	AuxLinear2,

	// frequency sensors
	AuxSpeed1,
	AuxSpeed2,

	// Let's always have all auxiliary sensors at the end - please add specific sensors above auxiliary

	// Leave me at the end!
	PlaceholderLast,
};
