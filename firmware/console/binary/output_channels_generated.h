#pragma once
#include "rusefi_types.h"
struct pid_status_s {
	// offset 0
	int16_t pTerm = (int16_t)0;
	// offset 2
	scaled_channel<int16_t, 100, 1> iTerm = (int16_t)0;
	// offset 4
	scaled_channel<int16_t, 100, 1> dTerm = (int16_t)0;
	// offset 6
	scaled_channel<int16_t, 100, 1> output = (int16_t)0;
	// offset 8
	scaled_channel<int16_t, 100, 1> error = (int16_t)0;
	// offset 10
	uint16_t resetCounter = (uint16_t)0;
};
static_assert(sizeof(pid_status_s) == 12);
static_assert(offsetof(pid_status_s, pTerm) == 0);
static_assert(offsetof(pid_status_s, iTerm) == 2);
static_assert(offsetof(pid_status_s, dTerm) == 4);
static_assert(offsetof(pid_status_s, output) == 6);
static_assert(offsetof(pid_status_s, error) == 8);
static_assert(offsetof(pid_status_s, resetCounter) == 10);

struct output_channels_s {
	// SD: Present
	// offset 0 bit 0
	bool sd_present : 1 {};
	// SD: Logging
	// offset 0 bit 1
	bool sd_logging_internal : 1 {};
	// offset 0 bit 2
	bool triggerScopeReady : 1 {};
	// offset 0 bit 3
	bool antilagTriggered : 1 {};
	// offset 0 bit 4
	bool isO2HeaterOn : 1 {};
	// offset 0 bit 5
	bool checkEngine : 1 {};
	// offset 0 bit 6
	bool needBurn : 1 {};
	// SD: MSD
	// offset 0 bit 7
	bool sd_msd : 1 {};
	// Harley ACR: Active
	// offset 0 bit 8
	bool acrActive : 1 {};
	// Tooth Logger Ready
	// offset 0 bit 9
	bool toothLogReady : 1 {};
	// Error: TPS1
	// offset 0 bit 10
	bool isTpsError : 1 {};
	// Error: CLT
	// offset 0 bit 11
	bool isCltError : 1 {};
	// Error: MAP
	// offset 0 bit 12
	bool isMapError : 1 {};
	// Error: IAT
	// offset 0 bit 13
	bool isIatError : 1 {};
	// Error: Trigger
	// offset 0 bit 14
	bool isTriggerError : 1 {};
	// Error: Active
	// offset 0 bit 15
	bool hasCriticalError : 1 {};
	// Warning: Active
	// offset 0 bit 16
	bool isWarnNow : 1 {};
	// Error: Pedal
	// offset 0 bit 17
	bool isPedalError : 1 {};
	// Launch control: Triggered
	// offset 0 bit 18
	bool launchTriggered : 1 {};
	// Error: TPS2
	// offset 0 bit 19
	bool isTps2Error : 1 {};
	// Error: Injector Fault
	// offset 0 bit 20
	bool injectorFault : 1 {};
	// Error: Ignition Fault
	// offset 0 bit 21
	bool ignitionFault : 1 {};
	// isUsbConnected
	// Original reason for this is to check if USB is connected from Lua
	// offset 0 bit 22
	bool isUsbConnected : 1 {};
	// DFCO: Active
	// offset 0 bit 23
	bool dfcoActive : 1 {};
	// offset 0 bit 24
	bool unusedBit_0_24 : 1 {};
	// offset 0 bit 25
	bool unusedBit_0_25 : 1 {};
	// offset 0 bit 26
	bool unusedBit_0_26 : 1 {};
	// offset 0 bit 27
	bool unusedBit_0_27 : 1 {};
	// offset 0 bit 28
	bool unusedBit_0_28 : 1 {};
	// offset 0 bit 29
	bool unusedBit_0_29 : 1 {};
	// offset 0 bit 30
	bool unusedBit_0_30 : 1 {};
	// offset 0 bit 31
	bool unusedBit_0_31 : 1 {};
	// RPM
	// RPM
	// offset 4
	uint16_t RPMValue = (uint16_t)0;
	// dRPM
	// RPM acceleration
	// offset 6
	int16_t rpmAcceleration = (int16_t)0;
	// Gearbox Ratio
	// value
	// offset 8
	scaled_channel<uint16_t, 100, 1> speedToRpmRatio = (uint16_t)0;
	// Vehicle Speed
	// kph
	// offset 10
	uint8_t vehicleSpeedKph = (uint8_t)0;
	// ECU temperature
	// deg C
	// offset 11
	int8_t internalMcuTemperature = (int8_t)0;
	// CLT
	// deg C
	// offset 12
	scaled_channel<int16_t, 100, 1> coolant = (int16_t)0;
	// IAT
	// deg C
	// offset 14
	scaled_channel<int16_t, 100, 1> intake = (int16_t)0;
	// Aux temp 1
	// deg C
	// offset 16
	scaled_channel<int16_t, 100, 1> auxTemp1 = (int16_t)0;
	// Aux temp 2
	// deg C
	// offset 18
	scaled_channel<int16_t, 100, 1> auxTemp2 = (int16_t)0;
	// TPS
	// %
	// offset 20
	scaled_channel<int16_t, 100, 1> TPSValue = (int16_t)0;
	// Throttle pedal position
	// %
	// offset 22
	scaled_channel<int16_t, 100, 1> throttlePedalPosition = (int16_t)0;
	// ADC
	// offset 24
	uint16_t tpsADC = (uint16_t)0;
	// Raw: MAF
	// V
	// offset 26
	scaled_channel<uint16_t, 1000, 1> rawMaf = (uint16_t)0;
	// MAF
	// kg/h
	// offset 28
	scaled_channel<uint16_t, 10, 1> mafMeasured = (uint16_t)0;
	// MAP
	// kPa
	// offset 30
	scaled_channel<uint16_t, 30, 1> MAPValue = (uint16_t)0;
	// kPa
	// offset 32
	scaled_channel<uint16_t, 30, 1> baroPressure = (uint16_t)0;
	// %
	// offset 34
	uint8_t widebandUpdateProgress = (uint8_t)0;
	// offset 35
	uint8_t orderingErrorCounter = (uint8_t)0;
	// VBatt
	// V
	// offset 36
	scaled_channel<uint16_t, 1000, 1> VBatt = (uint16_t)0;
	// Oil Pressure
	// kPa
	// offset 38
	scaled_channel<uint16_t, 30, 1> oilPressure = (uint16_t)0;
	// VVT: bank 1 intake
	// deg
	// offset 40
	scaled_channel<int16_t, 50, 1> vvtPositionB1I = (int16_t)0;
	// Fuel: Last inj pulse width
	// ms
	// offset 42
	scaled_channel<uint16_t, 300, 1> actualLastInjection = (uint16_t)0;
	// Fuel: injector duty cycle
	// %
	// offset 44
	scaled_channel<uint8_t, 2, 1> injectorDutyCycle = (uint8_t)0;
	// Fuel: VE
	// ratio
	// offset 45
	scaled_channel<uint8_t, 2, 1> veValue = (uint8_t)0;
	// Fuel: Injection timing SOI
	// deg
	// offset 46
	int16_t injectionOffset = (int16_t)0;
	// Fuel: Wall amount
	// mg
	// offset 48
	scaled_channel<uint16_t, 100, 1> wallFuelAmount = (uint16_t)0;
	// Fuel: Wall correction
	// mg
	// offset 50
	scaled_channel<int16_t, 100, 1> wallFuelCorrectionValue = (int16_t)0;
	// Trg: Revolution counter
	// offset 52
	uint16_t revolutionCounterSinceStart = (uint16_t)0;
	// CAN: Rx
	// offset 54
	uint16_t canReadCounter = (uint16_t)0;
	// Fuel: TPS AE add fuel ms
	// ms
	// offset 56
	scaled_channel<int16_t, 300, 1> tpsAccelFuel = (int16_t)0;
	// Ign: Timing Base
	// deg
	// offset 58
	scaled_channel<int16_t, 50, 1> ignitionAdvance = (int16_t)0;
	// Ign: Mode
	// offset 60
	uint8_t currentIgnitionMode = (uint8_t)0;
	// Fuel: Injection mode
	// offset 61
	uint8_t currentInjectionMode = (uint8_t)0;
	// Ign: Coil duty cycle
	// %
	// offset 62
	scaled_channel<uint16_t, 100, 1> coilDutyCycle = (uint16_t)0;
	// Fuel level
	// %
	// offset 64
	scaled_channel<int16_t, 100, 1> fuelTankLevel = (int16_t)0;
	// Fuel: Total consumed
	// grams
	// offset 66
	uint16_t totalFuelConsumption = (uint16_t)0;
	// Fuel: Flow rate
	// gram/s
	// offset 68
	scaled_channel<uint16_t, 200, 1> fuelFlowRate = (uint16_t)0;
	// TPS2
	// %
	// offset 70
	scaled_channel<int16_t, 100, 1> TPS2Value = (int16_t)0;
	// Uptime
	// sec
	// offset 72
	uint32_t seconds = (uint32_t)0;
	// firmware
	// version_f
	// offset 76
	uint32_t firmwareVersion = (uint32_t)0;
	// Raw: Wastegate position
	// V
	// offset 80
	scaled_channel<int16_t, 1000, 1> rawWastegatePosition = (int16_t)0;
	// Accel: Lateral
	// G
	// offset 82
	scaled_channel<int16_t, 1000, 1> accelerationLat = (int16_t)0;
	// Accel: Longitudinal
	// G
	// offset 84
	scaled_channel<int16_t, 1000, 1> accelerationLon = (int16_t)0;
	// Detected Gear
	// offset 86
	uint8_t detectedGear = (uint8_t)0;
	// offset 87
	uint8_t maxTriggerReentrant = (uint8_t)0;
	// Raw: Fuel press low
	// V
	// offset 88
	scaled_channel<int16_t, 1000, 1> rawLowFuelPressure = (int16_t)0;
	// Raw: Fuel press high
	// V
	// offset 90
	scaled_channel<int16_t, 1000, 1> rawHighFuelPressure = (int16_t)0;
	// Fuel pressure (low)
	// kpa
	// offset 92
	scaled_channel<int16_t, 30, 1> lowFuelPressure = (int16_t)0;
	// Fuel pressure (high)
	// bar
	// offset 94
	scaled_channel<int16_t, 10, 1> highFuelPressure = (int16_t)0;
	// Raw: Pedal secondary
	// V
	// offset 96
	scaled_channel<int16_t, 1000, 1> rawPpsSecondary = (int16_t)0;
	// TCU: Desired Gear
	// gear
	// offset 98
	int8_t tcuDesiredGear = (int8_t)0;
	// Flex Ethanol %
	// %
	// offset 99
	scaled_channel<uint8_t, 2, 1> flexPercent = (uint8_t)0;
	// Wastegate position sensor
	// %
	// offset 100
	scaled_channel<int16_t, 100, 1> wastegatePositionSensor = (int16_t)0;
	// Wheel speed: LF
	// offset 102
	uint8_t wheelSpeedLf = (uint8_t)0;
	// Wheel speed: RF
	// offset 103
	uint8_t wheelSpeedRf = (uint8_t)0;
	// offset 104
	float calibrationValue = (float)0;
	// offset 108
	uint8_t calibrationMode = (uint8_t)0;
	// Idle: Stepper target position
	// offset 109
	uint8_t idleStepperTargetPosition = (uint8_t)0;
	// Wheel speed: LR
	// offset 110
	uint8_t wheelSpeedLr = (uint8_t)0;
	// Wheel speed: RR
	// offset 111
	uint8_t wheelSpeedRr = (uint8_t)0;
	// Warning: counter
	// count
	// offset 112
	uint16_t warningCounter = (uint16_t)0;
	// Warning: last
	// error
	// offset 114
	uint16_t lastErrorCode = (uint16_t)0;
	// Air/Fuel Ratio
	// AFR
	// offset 116
	scaled_channel<uint16_t, 1000, 1> AFRValue = (uint16_t)0;
	// Air/Fuel Ratio 2
	// AFR
	// offset 118
	scaled_channel<uint16_t, 1000, 1> AFRValue2 = (uint16_t)0;
	// Raw: TPS 2 primary
	// V
	// offset 120
	scaled_channel<int16_t, 1000, 1> rawTps2Primary = (int16_t)0;
	// Raw: TPS 2 secondary
	// V
	// offset 122
	scaled_channel<int16_t, 1000, 1> rawTps2Secondary = (int16_t)0;
	// offset 124
	uint32_t tsConfigVersion = (uint32_t)0;
	// error
	// offset 128
	uint16_t recentErrorCode[8];
	// val
	// offset 144
	float debugFloatField1 = (float)0;
	// val
	// offset 148
	float debugFloatField2 = (float)0;
	// val
	// offset 152
	float debugFloatField3 = (float)0;
	// val
	// offset 156
	float debugFloatField4 = (float)0;
	// val
	// offset 160
	float debugFloatField5 = (float)0;
	// val
	// offset 164
	float debugFloatField6 = (float)0;
	// val
	// offset 168
	float debugFloatField7 = (float)0;
	// val
	// offset 172
	uint32_t debugIntField1 = (uint32_t)0;
	// val
	// offset 176
	uint32_t debugIntField2 = (uint32_t)0;
	// val
	// offset 180
	uint32_t debugIntField3 = (uint32_t)0;
	// val
	// offset 184
	int16_t debugIntField4 = (int16_t)0;
	// val
	// offset 186
	int16_t debugIntField5 = (int16_t)0;
	// EGT
	// deg C
	// offset 188
	uint16_t egt[8];
	// Raw: TPS 1 primary
	// V
	// offset 204
	scaled_channel<int16_t, 1000, 1> rawTps1Primary = (int16_t)0;
	// Raw: Pedal primary
	// V
	// offset 206
	scaled_channel<int16_t, 1000, 1> rawPpsPrimary = (int16_t)0;
	// Raw: CLT
	// V
	// offset 208
	scaled_channel<int16_t, 1000, 1> rawClt = (int16_t)0;
	// Raw: IAT
	// V
	// offset 210
	scaled_channel<int16_t, 1000, 1> rawIat = (int16_t)0;
	// Raw: OilP
	// V
	// offset 212
	scaled_channel<int16_t, 1000, 1> rawOilPressure = (int16_t)0;
	// offset 214
	uint8_t fuelClosedLoopBinIdx = (uint8_t)0;
	// Current Gear
	// gear
	// offset 215
	int8_t tcuCurrentGear = (int8_t)0;
	// Vss Accel
	// m/s2
	// offset 216
	scaled_channel<uint16_t, 300, 1> VssAcceleration = (uint16_t)0;
	// Lambda
	// offset 218
	scaled_channel<uint16_t, 10000, 1> lambdaValues[4];
	// VVT: bank 1 exhaust
	// deg
	// offset 226
	scaled_channel<int16_t, 50, 1> vvtPositionB1E = (int16_t)0;
	// VVT: bank 2 intake
	// deg
	// offset 228
	scaled_channel<int16_t, 50, 1> vvtPositionB2I = (int16_t)0;
	// VVT: bank 2 exhaust
	// deg
	// offset 230
	scaled_channel<int16_t, 50, 1> vvtPositionB2E = (int16_t)0;
	// Fuel: Trim bank
	// %
	// offset 232
	scaled_channel<int16_t, 100, 1> fuelPidCorrection[4];
	// Raw: TPS 1 secondary
	// V
	// offset 240
	scaled_channel<int16_t, 1000, 1> rawTps1Secondary = (int16_t)0;
	// Accel: Vertical
	// G
	// offset 242
	scaled_channel<int16_t, 1000, 1> accelerationVert = (int16_t)0;
	// Gyro: Yaw rate
	// deg/sec
	// offset 244
	scaled_channel<int16_t, 1000, 1> gyroYaw = (int16_t)0;
	// Turbocharger Speed
	// hz
	// offset 246
	uint16_t turboSpeed = (uint16_t)0;
	// Ign: Timing Cyl
	// deg
	// offset 248
	scaled_channel<int16_t, 50, 1> ignitionAdvanceCyl[12];
	// %
	// offset 272
	scaled_channel<int16_t, 100, 1> tps1Split = (int16_t)0;
	// %
	// offset 274
	scaled_channel<int16_t, 100, 1> tps2Split = (int16_t)0;
	// %
	// offset 276
	scaled_channel<int16_t, 100, 1> tps12Split = (int16_t)0;
	// %
	// offset 278
	scaled_channel<int16_t, 100, 1> accPedalSplit = (int16_t)0;
	// Ign: Cut Code
	// code
	// offset 280
	int8_t sparkCutReason = (int8_t)0;
	// Fuel: Cut Code
	// code
	// offset 281
	int8_t fuelCutReason = (int8_t)0;
	// rpm
	// offset 282
	uint16_t instantRpm = (uint16_t)0;
	// Raw: MAP
	// V
	// offset 284
	scaled_channel<uint16_t, 1000, 1> rawMap = (uint16_t)0;
	// Raw: AFR
	// V
	// offset 286
	scaled_channel<uint16_t, 1000, 1> rawAfr = (uint16_t)0;
	// Raw: Fuel level
	// V
	// offset 288
	scaled_channel<uint16_t, 1000, 1> rawFuelTankLevel = (uint16_t)0;
	// count
	// offset 290
	uint16_t testBenchIter = (uint16_t)0;
	// offset 292
	float calibrationValue2 = (float)0;
	// Lua: Last tick duration
	// us
	// offset 296
	uint16_t luaLastCycleDuration = (uint16_t)0;
	// Lua: Tick counter
	// count
	// offset 298
	uint8_t luaInvocationCounter = (uint8_t)0;
	// TCU: Current Range
	// offset 299
	uint8_t tcu_currentRange = (uint8_t)0;
	// TCU: Torque Converter Ratio
	// value
	// offset 300
	scaled_channel<uint16_t, 100, 1> tcRatio = (uint16_t)0;
	// offset 302
	uint8_t vssEdgeCounter = (uint8_t)0;
	// offset 303
	uint8_t issEdgeCounter = (uint8_t)0;
	// Aux linear 1
	// offset 304
	float auxLinear1 = (float)0;
	// Aux linear 2
	// offset 308
	float auxLinear2 = (float)0;
	// Aux linear 3
	// offset 312
	float auxLinear3 = (float)0;
	// Aux linear 4
	// offset 316
	float auxLinear4 = (float)0;
	// kPa
	// offset 320
	scaled_channel<uint16_t, 10, 1> fallbackMap = (uint16_t)0;
	// Instant MAP
	// kPa
	// offset 322
	scaled_channel<uint16_t, 30, 1> instantMAPValue = (uint16_t)0;
	// us
	// offset 324
	uint16_t maxLockedDuration = (uint16_t)0;
	// CAN: Tx OK
	// offset 326
	uint16_t canWriteOk = (uint16_t)0;
	// CAN: Tx err
	// offset 328
	uint16_t canWriteNotOk = (uint16_t)0;
	// offset 330
	uint8_t starterState = (uint8_t)0;
	// offset 331
	uint8_t starterRelayDisable = (uint8_t)0;
	// Ign: Multispark count
	// offset 332
	uint8_t multiSparkCounter = (uint8_t)0;
	// offset 333
	uint8_t extiOverflowCount = (uint8_t)0;
	// TCU: Input Shaft Speed
	// RPM
	// offset 334
	uint16_t ISSValue = (uint16_t)0;
	// offset 336
	pid_status_s alternatorStatus;
	// offset 348
	pid_status_s idleStatus;
	// offset 360
	pid_status_s etbStatus;
	// offset 372
	pid_status_s boostStatus;
	// offset 384
	pid_status_s wastegateDcStatus;
	// Aux speed 1
	// s
	// offset 396
	uint16_t auxSpeed1 = (uint16_t)0;
	// Aux speed 2
	// s
	// offset 398
	uint16_t auxSpeed2 = (uint16_t)0;
	// V
	// offset 400
	scaled_channel<int16_t, 1000, 1> rawAnalogInput[8];
	// GPPWM Output
	// %
	// offset 416
	scaled_channel<uint8_t, 2, 1> gppwmOutput[4];
	// offset 420
	int16_t gppwmXAxis[4];
	// offset 428
	scaled_channel<int16_t, 10, 1> gppwmYAxis[4];
	// Raw: Vbatt
	// V
	// offset 436
	scaled_channel<int16_t, 1000, 1> rawBattery = (int16_t)0;
	// offset 438
	scaled_channel<int16_t, 10, 1> ignBlendParameter[4];
	// %
	// offset 446
	scaled_channel<uint8_t, 2, 1> ignBlendBias[4];
	// deg
	// offset 450
	scaled_channel<int16_t, 100, 1> ignBlendOutput[4];
	// offset 458
	scaled_channel<int16_t, 10, 1> ignBlendYAxis[4];
	// offset 466
	scaled_channel<int16_t, 10, 1> veBlendParameter[4];
	// %
	// offset 474
	scaled_channel<uint8_t, 2, 1> veBlendBias[4];
	// %
	// offset 478
	scaled_channel<int16_t, 100, 1> veBlendOutput[4];
	// offset 486
	scaled_channel<int16_t, 10, 1> veBlendYAxis[4];
	// offset 494
	scaled_channel<int16_t, 10, 1> boostOpenLoopBlendParameter[2];
	// %
	// offset 498
	scaled_channel<uint8_t, 2, 1> boostOpenLoopBlendBias[2];
	// %
	// offset 500
	int8_t boostOpenLoopBlendOutput[2];
	// offset 502
	scaled_channel<int16_t, 10, 1> boostOpenLoopBlendYAxis[2];
	// offset 506
	scaled_channel<int16_t, 10, 1> boostClosedLoopBlendParameter[2];
	// %
	// offset 510
	scaled_channel<uint8_t, 2, 1> boostClosedLoopBlendBias[2];
	// %
	// offset 512
	scaled_channel<int16_t, 10, 1> boostClosedLoopBlendOutput[2];
	// offset 516
	scaled_channel<int16_t, 10, 1> boostClosedLoopBlendYAxis[2];
	// kPa
	// offset 520
	scaled_channel<uint16_t, 30, 1> mapFast = (uint16_t)0;
	// %
	// offset 522
	scaled_channel<uint16_t, 100, 1> Gego = (uint16_t)0;
	// Lua: Gauge
	// value
	// offset 524
	float luaGauges[2];
	// Raw: MAF 2
	// V
	// offset 532
	scaled_channel<uint16_t, 1000, 1> rawMaf2 = (uint16_t)0;
	// MAF #2
	// kg/h
	// offset 534
	scaled_channel<uint16_t, 10, 1> mafMeasured2 = (uint16_t)0;
	// deg C
	// offset 536
	scaled_channel<int16_t, 100, 1> oilTemp = (int16_t)0;
	// deg C
	// offset 538
	scaled_channel<int16_t, 100, 1> fuelTemp = (int16_t)0;
	// deg C
	// offset 540
	scaled_channel<int16_t, 100, 1> ambientTemp = (int16_t)0;
	// deg C
	// offset 542
	scaled_channel<int16_t, 100, 1> compressorDischargeTemp = (int16_t)0;
	// kPa
	// offset 544
	scaled_channel<uint16_t, 30, 1> compressorDischargePressure = (uint16_t)0;
	// kPa
	// offset 546
	scaled_channel<uint16_t, 30, 1> throttleInletPressure = (uint16_t)0;
	// sec
	// offset 548
	uint16_t ignitionOnTime = (uint16_t)0;
	// sec
	// offset 550
	uint16_t engineRunTime = (uint16_t)0;
	// km
	// offset 552
	scaled_channel<uint16_t, 10, 1> distanceTraveled = (uint16_t)0;
	// Air/Fuel Ratio (Gas Scale)
	// AFR
	// offset 554
	scaled_channel<uint16_t, 1000, 1> afrGasolineScale = (uint16_t)0;
	// Air/Fuel Ratio 2 (Gas Scale)
	// AFR
	// offset 556
	scaled_channel<uint16_t, 1000, 1> afr2GasolineScale = (uint16_t)0;
	// Fuel: Last inj pulse width stg 2
	// ms
	// offset 558
	scaled_channel<uint16_t, 300, 1> actualLastInjectionStage2 = (uint16_t)0;
	// Fuel: injector duty cycle stage 2
	// %
	// offset 560
	scaled_channel<uint8_t, 2, 1> injectorDutyCycleStage2 = (uint8_t)0;
	// offset 561
	uint8_t schedulingUsedCount = (uint8_t)0;
	// offset 562
	uint16_t mapAveragingSamples = (uint16_t)0;
	// ratio
	// offset 564
	scaled_channel<uint16_t, 1000, 1> dwellAccuracyRatio = (uint16_t)0;
	// MAF: Pre-filter
	// kg/h
	// offset 566
	scaled_channel<uint16_t, 10, 1> mafMeasured_preFilter = (uint16_t)0;
	// rpm
	// offset 568
	uint16_t cylinderRpm[12];
	// rpm
	// offset 592
	int8_t cylinderRpmDelta[12];
};
static_assert(sizeof(output_channels_s) == 604);
static_assert(offsetof(output_channels_s, RPMValue) == 4);
static_assert(offsetof(output_channels_s, rpmAcceleration) == 6);
static_assert(offsetof(output_channels_s, speedToRpmRatio) == 8);
static_assert(offsetof(output_channels_s, vehicleSpeedKph) == 10);
static_assert(offsetof(output_channels_s, internalMcuTemperature) == 11);
static_assert(offsetof(output_channels_s, coolant) == 12);
static_assert(offsetof(output_channels_s, intake) == 14);
static_assert(offsetof(output_channels_s, auxTemp1) == 16);
static_assert(offsetof(output_channels_s, auxTemp2) == 18);
static_assert(offsetof(output_channels_s, TPSValue) == 20);
static_assert(offsetof(output_channels_s, throttlePedalPosition) == 22);
static_assert(offsetof(output_channels_s, tpsADC) == 24);
static_assert(offsetof(output_channels_s, rawMaf) == 26);
static_assert(offsetof(output_channels_s, mafMeasured) == 28);
static_assert(offsetof(output_channels_s, MAPValue) == 30);
static_assert(offsetof(output_channels_s, baroPressure) == 32);
static_assert(offsetof(output_channels_s, widebandUpdateProgress) == 34);
static_assert(offsetof(output_channels_s, orderingErrorCounter) == 35);
static_assert(offsetof(output_channels_s, VBatt) == 36);
static_assert(offsetof(output_channels_s, oilPressure) == 38);
static_assert(offsetof(output_channels_s, vvtPositionB1I) == 40);
static_assert(offsetof(output_channels_s, actualLastInjection) == 42);
static_assert(offsetof(output_channels_s, injectorDutyCycle) == 44);
static_assert(offsetof(output_channels_s, veValue) == 45);
static_assert(offsetof(output_channels_s, injectionOffset) == 46);
static_assert(offsetof(output_channels_s, wallFuelAmount) == 48);
static_assert(offsetof(output_channels_s, wallFuelCorrectionValue) == 50);
static_assert(offsetof(output_channels_s, revolutionCounterSinceStart) == 52);
static_assert(offsetof(output_channels_s, canReadCounter) == 54);
static_assert(offsetof(output_channels_s, tpsAccelFuel) == 56);
static_assert(offsetof(output_channels_s, ignitionAdvance) == 58);
static_assert(offsetof(output_channels_s, currentIgnitionMode) == 60);
static_assert(offsetof(output_channels_s, currentInjectionMode) == 61);
static_assert(offsetof(output_channels_s, coilDutyCycle) == 62);
static_assert(offsetof(output_channels_s, fuelTankLevel) == 64);
static_assert(offsetof(output_channels_s, totalFuelConsumption) == 66);
static_assert(offsetof(output_channels_s, fuelFlowRate) == 68);
static_assert(offsetof(output_channels_s, TPS2Value) == 70);
static_assert(offsetof(output_channels_s, seconds) == 72);
static_assert(offsetof(output_channels_s, firmwareVersion) == 76);
static_assert(offsetof(output_channels_s, rawWastegatePosition) == 80);
static_assert(offsetof(output_channels_s, accelerationLat) == 82);
static_assert(offsetof(output_channels_s, accelerationLon) == 84);
static_assert(offsetof(output_channels_s, detectedGear) == 86);
static_assert(offsetof(output_channels_s, maxTriggerReentrant) == 87);
static_assert(offsetof(output_channels_s, rawLowFuelPressure) == 88);
static_assert(offsetof(output_channels_s, rawHighFuelPressure) == 90);
static_assert(offsetof(output_channels_s, lowFuelPressure) == 92);
static_assert(offsetof(output_channels_s, highFuelPressure) == 94);
static_assert(offsetof(output_channels_s, rawPpsSecondary) == 96);
static_assert(offsetof(output_channels_s, tcuDesiredGear) == 98);
static_assert(offsetof(output_channels_s, flexPercent) == 99);
static_assert(offsetof(output_channels_s, wastegatePositionSensor) == 100);
static_assert(offsetof(output_channels_s, wheelSpeedLf) == 102);
static_assert(offsetof(output_channels_s, wheelSpeedRf) == 103);
static_assert(offsetof(output_channels_s, calibrationValue) == 104);
static_assert(offsetof(output_channels_s, calibrationMode) == 108);
static_assert(offsetof(output_channels_s, idleStepperTargetPosition) == 109);
static_assert(offsetof(output_channels_s, wheelSpeedLr) == 110);
static_assert(offsetof(output_channels_s, wheelSpeedRr) == 111);
static_assert(offsetof(output_channels_s, warningCounter) == 112);
static_assert(offsetof(output_channels_s, lastErrorCode) == 114);
static_assert(offsetof(output_channels_s, AFRValue) == 116);
static_assert(offsetof(output_channels_s, AFRValue2) == 118);
static_assert(offsetof(output_channels_s, rawTps2Primary) == 120);
static_assert(offsetof(output_channels_s, rawTps2Secondary) == 122);
static_assert(offsetof(output_channels_s, tsConfigVersion) == 124);
static_assert(offsetof(output_channels_s, recentErrorCode) == 128);
static_assert(offsetof(output_channels_s, debugFloatField1) == 144);
static_assert(offsetof(output_channels_s, debugFloatField2) == 148);
static_assert(offsetof(output_channels_s, debugFloatField3) == 152);
static_assert(offsetof(output_channels_s, debugFloatField4) == 156);
static_assert(offsetof(output_channels_s, debugFloatField5) == 160);
static_assert(offsetof(output_channels_s, debugFloatField6) == 164);
static_assert(offsetof(output_channels_s, debugFloatField7) == 168);
static_assert(offsetof(output_channels_s, debugIntField1) == 172);
static_assert(offsetof(output_channels_s, debugIntField2) == 176);
static_assert(offsetof(output_channels_s, debugIntField3) == 180);
static_assert(offsetof(output_channels_s, debugIntField4) == 184);
static_assert(offsetof(output_channels_s, debugIntField5) == 186);
static_assert(offsetof(output_channels_s, egt) == 188);
static_assert(offsetof(output_channels_s, rawTps1Primary) == 204);
static_assert(offsetof(output_channels_s, rawPpsPrimary) == 206);
static_assert(offsetof(output_channels_s, rawClt) == 208);
static_assert(offsetof(output_channels_s, rawIat) == 210);
static_assert(offsetof(output_channels_s, rawOilPressure) == 212);
static_assert(offsetof(output_channels_s, fuelClosedLoopBinIdx) == 214);
static_assert(offsetof(output_channels_s, tcuCurrentGear) == 215);
static_assert(offsetof(output_channels_s, VssAcceleration) == 216);
static_assert(offsetof(output_channels_s, lambdaValues) == 218);
static_assert(offsetof(output_channels_s, vvtPositionB1E) == 226);
static_assert(offsetof(output_channels_s, vvtPositionB2I) == 228);
static_assert(offsetof(output_channels_s, vvtPositionB2E) == 230);
static_assert(offsetof(output_channels_s, fuelPidCorrection) == 232);
static_assert(offsetof(output_channels_s, rawTps1Secondary) == 240);
static_assert(offsetof(output_channels_s, accelerationVert) == 242);
static_assert(offsetof(output_channels_s, gyroYaw) == 244);
static_assert(offsetof(output_channels_s, turboSpeed) == 246);
static_assert(offsetof(output_channels_s, ignitionAdvanceCyl) == 248);
static_assert(offsetof(output_channels_s, tps1Split) == 272);
static_assert(offsetof(output_channels_s, tps2Split) == 274);
static_assert(offsetof(output_channels_s, tps12Split) == 276);
static_assert(offsetof(output_channels_s, accPedalSplit) == 278);
static_assert(offsetof(output_channels_s, sparkCutReason) == 280);
static_assert(offsetof(output_channels_s, fuelCutReason) == 281);
static_assert(offsetof(output_channels_s, instantRpm) == 282);
static_assert(offsetof(output_channels_s, rawMap) == 284);
static_assert(offsetof(output_channels_s, rawAfr) == 286);
static_assert(offsetof(output_channels_s, rawFuelTankLevel) == 288);
static_assert(offsetof(output_channels_s, testBenchIter) == 290);
static_assert(offsetof(output_channels_s, calibrationValue2) == 292);
static_assert(offsetof(output_channels_s, luaLastCycleDuration) == 296);
static_assert(offsetof(output_channels_s, luaInvocationCounter) == 298);
static_assert(offsetof(output_channels_s, tcu_currentRange) == 299);
static_assert(offsetof(output_channels_s, tcRatio) == 300);
static_assert(offsetof(output_channels_s, vssEdgeCounter) == 302);
static_assert(offsetof(output_channels_s, issEdgeCounter) == 303);
static_assert(offsetof(output_channels_s, auxLinear1) == 304);
static_assert(offsetof(output_channels_s, auxLinear2) == 308);
static_assert(offsetof(output_channels_s, auxLinear3) == 312);
static_assert(offsetof(output_channels_s, auxLinear4) == 316);
static_assert(offsetof(output_channels_s, fallbackMap) == 320);
static_assert(offsetof(output_channels_s, instantMAPValue) == 322);
static_assert(offsetof(output_channels_s, maxLockedDuration) == 324);
static_assert(offsetof(output_channels_s, canWriteOk) == 326);
static_assert(offsetof(output_channels_s, canWriteNotOk) == 328);
static_assert(offsetof(output_channels_s, starterState) == 330);
static_assert(offsetof(output_channels_s, starterRelayDisable) == 331);
static_assert(offsetof(output_channels_s, multiSparkCounter) == 332);
static_assert(offsetof(output_channels_s, extiOverflowCount) == 333);
static_assert(offsetof(output_channels_s, ISSValue) == 334);
static_assert(offsetof(output_channels_s, auxSpeed1) == 396);
static_assert(offsetof(output_channels_s, auxSpeed2) == 398);
static_assert(offsetof(output_channels_s, rawAnalogInput) == 400);
static_assert(offsetof(output_channels_s, gppwmOutput) == 416);
static_assert(offsetof(output_channels_s, gppwmXAxis) == 420);
static_assert(offsetof(output_channels_s, gppwmYAxis) == 428);
static_assert(offsetof(output_channels_s, rawBattery) == 436);
static_assert(offsetof(output_channels_s, ignBlendParameter) == 438);
static_assert(offsetof(output_channels_s, ignBlendBias) == 446);
static_assert(offsetof(output_channels_s, ignBlendOutput) == 450);
static_assert(offsetof(output_channels_s, ignBlendYAxis) == 458);
static_assert(offsetof(output_channels_s, veBlendParameter) == 466);
static_assert(offsetof(output_channels_s, veBlendBias) == 474);
static_assert(offsetof(output_channels_s, veBlendOutput) == 478);
static_assert(offsetof(output_channels_s, veBlendYAxis) == 486);
static_assert(offsetof(output_channels_s, boostOpenLoopBlendParameter) == 494);
static_assert(offsetof(output_channels_s, boostOpenLoopBlendBias) == 498);
static_assert(offsetof(output_channels_s, boostOpenLoopBlendOutput) == 500);
static_assert(offsetof(output_channels_s, boostOpenLoopBlendYAxis) == 502);
static_assert(offsetof(output_channels_s, boostClosedLoopBlendParameter) == 506);
static_assert(offsetof(output_channels_s, boostClosedLoopBlendBias) == 510);
static_assert(offsetof(output_channels_s, boostClosedLoopBlendOutput) == 512);
static_assert(offsetof(output_channels_s, boostClosedLoopBlendYAxis) == 516);
static_assert(offsetof(output_channels_s, mapFast) == 520);
static_assert(offsetof(output_channels_s, Gego) == 522);
static_assert(offsetof(output_channels_s, luaGauges) == 524);
static_assert(offsetof(output_channels_s, rawMaf2) == 532);
static_assert(offsetof(output_channels_s, mafMeasured2) == 534);
static_assert(offsetof(output_channels_s, oilTemp) == 536);
static_assert(offsetof(output_channels_s, fuelTemp) == 538);
static_assert(offsetof(output_channels_s, ambientTemp) == 540);
static_assert(offsetof(output_channels_s, compressorDischargeTemp) == 542);
static_assert(offsetof(output_channels_s, compressorDischargePressure) == 544);
static_assert(offsetof(output_channels_s, throttleInletPressure) == 546);
static_assert(offsetof(output_channels_s, ignitionOnTime) == 548);
static_assert(offsetof(output_channels_s, engineRunTime) == 550);
static_assert(offsetof(output_channels_s, distanceTraveled) == 552);
static_assert(offsetof(output_channels_s, afrGasolineScale) == 554);
static_assert(offsetof(output_channels_s, afr2GasolineScale) == 556);
static_assert(offsetof(output_channels_s, actualLastInjectionStage2) == 558);
static_assert(offsetof(output_channels_s, injectorDutyCycleStage2) == 560);
static_assert(offsetof(output_channels_s, schedulingUsedCount) == 561);
static_assert(offsetof(output_channels_s, mapAveragingSamples) == 562);
static_assert(offsetof(output_channels_s, dwellAccuracyRatio) == 564);
static_assert(offsetof(output_channels_s, mafMeasured_preFilter) == 566);
static_assert(offsetof(output_channels_s, cylinderRpm) == 568);
static_assert(offsetof(output_channels_s, cylinderRpmDelta) == 592);

