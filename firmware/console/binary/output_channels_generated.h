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
	uint8_t unused35 = (uint8_t)0;
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
	// offset 64
	uint16_t unused66 = (uint16_t)0;
	// offset 66
	uint16_t unused68 = (uint16_t)0;
	// Fuel level
	// %
	// offset 68
	scaled_channel<int16_t, 100, 1> fuelTankLevel = (int16_t)0;
	// Fuel: Total consumed
	// grams
	// offset 70
	uint16_t totalFuelConsumption = (uint16_t)0;
	// Fuel: Flow rate
	// gram/s
	// offset 72
	scaled_channel<uint16_t, 200, 1> fuelFlowRate = (uint16_t)0;
	// TPS2
	// %
	// offset 74
	scaled_channel<int16_t, 100, 1> TPS2Value = (int16_t)0;
	// Uptime
	// sec
	// offset 76
	uint32_t seconds = (uint32_t)0;
	// Engine Mode
	// em
	// offset 80
	uint32_t engineMode = (uint32_t)0;
	// firmware
	// version_f
	// offset 84
	uint32_t firmwareVersion = (uint32_t)0;
	// Raw: Wastegate position
	// V
	// offset 88
	scaled_channel<int16_t, 1000, 1> rawWastegatePosition = (int16_t)0;
	// Accel: Lateral
	// G
	// offset 90
	scaled_channel<int16_t, 1000, 1> accelerationLat = (int16_t)0;
	// Accel: Longitudinal
	// G
	// offset 92
	scaled_channel<int16_t, 1000, 1> accelerationLon = (int16_t)0;
	// Detected Gear
	// offset 94
	uint8_t detectedGear = (uint8_t)0;
	// offset 95
	uint8_t maxTriggerReentrant = (uint8_t)0;
	// Raw: Fuel press low
	// V
	// offset 96
	scaled_channel<int16_t, 1000, 1> rawLowFuelPressure = (int16_t)0;
	// Raw: Fuel press high
	// V
	// offset 98
	scaled_channel<int16_t, 1000, 1> rawHighFuelPressure = (int16_t)0;
	// Fuel pressure (low)
	// kpa
	// offset 100
	scaled_channel<int16_t, 30, 1> lowFuelPressure = (int16_t)0;
	// Fuel pressure (high)
	// bar
	// offset 102
	scaled_channel<int16_t, 10, 1> highFuelPressure = (int16_t)0;
	// Raw: Pedal secondary
	// V
	// offset 104
	scaled_channel<int16_t, 1000, 1> rawPpsSecondary = (int16_t)0;
	// TCU: Desired Gear
	// gear
	// offset 106
	int8_t tcuDesiredGear = (int8_t)0;
	// Flex Ethanol %
	// %
	// offset 107
	scaled_channel<uint8_t, 2, 1> flexPercent = (uint8_t)0;
	// Wastegate position sensor
	// %
	// offset 108
	scaled_channel<int16_t, 100, 1> wastegatePositionSensor = (int16_t)0;
	// Wheel speed: LF
	// offset 110
	uint8_t wheelSpeedLf = (uint8_t)0;
	// Wheel speed: RF
	// offset 111
	uint8_t wheelSpeedRf = (uint8_t)0;
	// offset 112
	float calibrationValue = (float)0;
	// offset 116
	uint8_t calibrationMode = (uint8_t)0;
	// Idle: Stepper target position
	// offset 117
	uint8_t idleStepperTargetPosition = (uint8_t)0;
	// Wheel speed: LR
	// offset 118
	uint8_t wheelSpeedLr = (uint8_t)0;
	// Wheel speed: RR
	// offset 119
	uint8_t wheelSpeedRr = (uint8_t)0;
	// offset 120
	uint32_t orderingErrorCounter = (uint32_t)0;
	// offset 124
	uint32_t tsConfigVersion = (uint32_t)0;
	// Warning: counter
	// count
	// offset 128
	uint16_t warningCounter = (uint16_t)0;
	// Warning: last
	// error
	// offset 130
	uint16_t lastErrorCode = (uint16_t)0;
	// error
	// offset 132
	uint16_t recentErrorCode[8];
	// val
	// offset 148
	float debugFloatField1 = (float)0;
	// val
	// offset 152
	float debugFloatField2 = (float)0;
	// val
	// offset 156
	float debugFloatField3 = (float)0;
	// val
	// offset 160
	float debugFloatField4 = (float)0;
	// val
	// offset 164
	float debugFloatField5 = (float)0;
	// val
	// offset 168
	float debugFloatField6 = (float)0;
	// val
	// offset 172
	float debugFloatField7 = (float)0;
	// val
	// offset 176
	uint32_t debugIntField1 = (uint32_t)0;
	// val
	// offset 180
	uint32_t debugIntField2 = (uint32_t)0;
	// val
	// offset 184
	uint32_t debugIntField3 = (uint32_t)0;
	// val
	// offset 188
	int16_t debugIntField4 = (int16_t)0;
	// val
	// offset 190
	int16_t debugIntField5 = (int16_t)0;
	// EGT
	// deg C
	// offset 192
	uint16_t egt[8];
	// Raw: TPS 1 primary
	// V
	// offset 208
	scaled_channel<int16_t, 1000, 1> rawTps1Primary = (int16_t)0;
	// Raw: Pedal primary
	// V
	// offset 210
	scaled_channel<int16_t, 1000, 1> rawPpsPrimary = (int16_t)0;
	// Raw: CLT
	// V
	// offset 212
	scaled_channel<int16_t, 1000, 1> rawClt = (int16_t)0;
	// Raw: IAT
	// V
	// offset 214
	scaled_channel<int16_t, 1000, 1> rawIat = (int16_t)0;
	// Raw: OilP
	// V
	// offset 216
	scaled_channel<int16_t, 1000, 1> rawOilPressure = (int16_t)0;
	// offset 218
	uint8_t fuelClosedLoopBinIdx = (uint8_t)0;
	// Current Gear
	// gear
	// offset 219
	int8_t tcuCurrentGear = (int8_t)0;
	// Air/Fuel Ratio
	// AFR
	// offset 220
	scaled_channel<uint16_t, 1000, 1> AFRValue = (uint16_t)0;
	// Vss Accel
	// m/s2
	// offset 222
	scaled_channel<uint16_t, 300, 1> VssAcceleration = (uint16_t)0;
	// Lambda
	// offset 224
	scaled_channel<uint16_t, 10000, 1> lambdaValues[4];
	// Air/Fuel Ratio 2
	// AFR
	// offset 232
	scaled_channel<uint16_t, 1000, 1> AFRValue2 = (uint16_t)0;
	// VVT: bank 1 exhaust
	// deg
	// offset 234
	scaled_channel<int16_t, 50, 1> vvtPositionB1E = (int16_t)0;
	// VVT: bank 2 intake
	// deg
	// offset 236
	scaled_channel<int16_t, 50, 1> vvtPositionB2I = (int16_t)0;
	// VVT: bank 2 exhaust
	// deg
	// offset 238
	scaled_channel<int16_t, 50, 1> vvtPositionB2E = (int16_t)0;
	// Fuel: Trim bank
	// %
	// offset 240
	scaled_channel<int16_t, 100, 1> fuelPidCorrection[4];
	// Raw: TPS 1 secondary
	// V
	// offset 248
	scaled_channel<int16_t, 1000, 1> rawTps1Secondary = (int16_t)0;
	// Raw: TPS 2 primary
	// V
	// offset 250
	scaled_channel<int16_t, 1000, 1> rawTps2Primary = (int16_t)0;
	// Raw: TPS 2 secondary
	// V
	// offset 252
	scaled_channel<int16_t, 1000, 1> rawTps2Secondary = (int16_t)0;
	// Accel: Vertical
	// G
	// offset 254
	scaled_channel<int16_t, 1000, 1> accelerationVert = (int16_t)0;
	// Gyro: Yaw rate
	// deg/sec
	// offset 256
	scaled_channel<int16_t, 1000, 1> gyroYaw = (int16_t)0;
	// Turbocharger Speed
	// hz
	// offset 258
	uint16_t turboSpeed = (uint16_t)0;
	// Ign: Timing Cyl
	// deg
	// offset 260
	scaled_channel<int16_t, 50, 1> ignitionAdvanceCyl[12];
	// %
	// offset 284
	scaled_channel<int16_t, 100, 1> tps1Split = (int16_t)0;
	// %
	// offset 286
	scaled_channel<int16_t, 100, 1> tps2Split = (int16_t)0;
	// %
	// offset 288
	scaled_channel<int16_t, 100, 1> tps12Split = (int16_t)0;
	// %
	// offset 290
	scaled_channel<int16_t, 100, 1> accPedalSplit = (int16_t)0;
	// Ign: Cut Code
	// code
	// offset 292
	int8_t sparkCutReason = (int8_t)0;
	// Fuel: Cut Code
	// code
	// offset 293
	int8_t fuelCutReason = (int8_t)0;
	// rpm
	// offset 294
	uint16_t instantRpm = (uint16_t)0;
	// Raw: MAP
	// V
	// offset 296
	scaled_channel<uint16_t, 1000, 1> rawMap = (uint16_t)0;
	// Raw: AFR
	// V
	// offset 298
	scaled_channel<uint16_t, 1000, 1> rawAfr = (uint16_t)0;
	// Raw: Fuel level
	// V
	// offset 300
	scaled_channel<uint16_t, 1000, 1> rawFuelTankLevel = (uint16_t)0;
	// offset 302
	uint8_t alignmentFill_at_302[2];
	// offset 304
	float calibrationValue2 = (float)0;
	// Lua: Tick counter
	// count
	// offset 308
	uint32_t luaInvocationCounter = (uint32_t)0;
	// Lua: Last tick duration
	// nt
	// offset 312
	uint32_t luaLastCycleDuration = (uint32_t)0;
	// TCU: Current Range
	// offset 316
	uint8_t tcu_currentRange = (uint8_t)0;
	// offset 317
	uint8_t alignmentFill_at_317[1];
	// TCU: Torque Converter Ratio
	// value
	// offset 318
	scaled_channel<uint16_t, 100, 1> tcRatio = (uint16_t)0;
	// offset 320
	uint32_t vssEdgeCounter = (uint32_t)0;
	// offset 324
	uint32_t issEdgeCounter = (uint32_t)0;
	// Aux linear 1
	// offset 328
	float auxLinear1 = (float)0;
	// Aux linear 2
	// offset 332
	float auxLinear2 = (float)0;
	// Aux linear 3
	// offset 336
	float auxLinear3 = (float)0;
	// Aux linear 4
	// offset 340
	float auxLinear4 = (float)0;
	// kPa
	// offset 344
	scaled_channel<uint16_t, 10, 1> fallbackMap = (uint16_t)0;
	// Instant MAP
	// kPa
	// offset 346
	scaled_channel<uint16_t, 30, 1> instantMAPValue = (uint16_t)0;
	// us
	// offset 348
	uint16_t maxLockedDuration = (uint16_t)0;
	// CAN: Tx OK
	// offset 350
	uint16_t canWriteOk = (uint16_t)0;
	// CAN: Tx err
	// offset 352
	uint16_t canWriteNotOk = (uint16_t)0;
	// offset 354
	uint8_t starterState = (uint8_t)0;
	// offset 355
	uint8_t starterRelayDisable = (uint8_t)0;
	// Ign: Multispark count
	// offset 356
	uint8_t multiSparkCounter = (uint8_t)0;
	// offset 357
	uint8_t extiOverflowCount = (uint8_t)0;
	// offset 358
	uint8_t alignmentFill_at_358[2];
	// offset 360
	pid_status_s alternatorStatus;
	// offset 372
	pid_status_s idleStatus;
	// offset 384
	pid_status_s etbStatus;
	// offset 396
	pid_status_s boostStatus;
	// offset 408
	pid_status_s wastegateDcStatus;
	// Aux speed 1
	// s
	// offset 420
	uint16_t auxSpeed1 = (uint16_t)0;
	// Aux speed 2
	// s
	// offset 422
	uint16_t auxSpeed2 = (uint16_t)0;
	// TCU: Input Shaft Speed
	// RPM
	// offset 424
	uint16_t ISSValue = (uint16_t)0;
	// V
	// offset 426
	scaled_channel<int16_t, 1000, 1> rawAnalogInput[8];
	// GPPWM Output
	// %
	// offset 442
	scaled_channel<uint8_t, 2, 1> gppwmOutput[4];
	// offset 446
	int16_t gppwmXAxis[4];
	// offset 454
	scaled_channel<int16_t, 10, 1> gppwmYAxis[4];
	// Raw: Vbatt
	// V
	// offset 462
	scaled_channel<int16_t, 1000, 1> rawBattery = (int16_t)0;
	// offset 464
	scaled_channel<int16_t, 10, 1> ignBlendParameter[4];
	// %
	// offset 472
	scaled_channel<uint8_t, 2, 1> ignBlendBias[4];
	// deg
	// offset 476
	scaled_channel<int16_t, 100, 1> ignBlendOutput[4];
	// offset 484
	scaled_channel<int16_t, 10, 1> ignBlendYAxis[4];
	// offset 492
	scaled_channel<int16_t, 10, 1> veBlendParameter[4];
	// %
	// offset 500
	scaled_channel<uint8_t, 2, 1> veBlendBias[4];
	// %
	// offset 504
	scaled_channel<int16_t, 100, 1> veBlendOutput[4];
	// offset 512
	scaled_channel<int16_t, 10, 1> veBlendYAxis[4];
	// offset 520
	scaled_channel<int16_t, 10, 1> boostOpenLoopBlendParameter[2];
	// %
	// offset 524
	scaled_channel<uint8_t, 2, 1> boostOpenLoopBlendBias[2];
	// %
	// offset 526
	int8_t boostOpenLoopBlendOutput[2];
	// offset 528
	scaled_channel<int16_t, 10, 1> boostOpenLoopBlendYAxis[2];
	// offset 532
	scaled_channel<int16_t, 10, 1> boostClosedLoopBlendParameter[2];
	// %
	// offset 536
	scaled_channel<uint8_t, 2, 1> boostClosedLoopBlendBias[2];
	// %
	// offset 538
	scaled_channel<int16_t, 10, 1> boostClosedLoopBlendOutput[2];
	// offset 542
	scaled_channel<int16_t, 10, 1> boostClosedLoopBlendYAxis[2];
	// kPa
	// offset 546
	scaled_channel<uint16_t, 30, 1> mapFast = (uint16_t)0;
	// offset 548
	uint32_t outputRequestPeriod = (uint32_t)0;
	// Lua: Gauge
	// value
	// offset 552
	float luaGauges[2];
	// Raw: MAF 2
	// V
	// offset 560
	scaled_channel<uint16_t, 1000, 1> rawMaf2 = (uint16_t)0;
	// MAF #2
	// kg/h
	// offset 562
	scaled_channel<uint16_t, 10, 1> mafMeasured2 = (uint16_t)0;
	// offset 564
	uint16_t schedulingUsedCount = (uint16_t)0;
	// %
	// offset 566
	scaled_channel<uint16_t, 100, 1> Gego = (uint16_t)0;
	// count
	// offset 568
	uint16_t testBenchIter = (uint16_t)0;
	// deg C
	// offset 570
	scaled_channel<int16_t, 100, 1> oilTemp = (int16_t)0;
	// deg C
	// offset 572
	scaled_channel<int16_t, 100, 1> fuelTemp = (int16_t)0;
	// deg C
	// offset 574
	scaled_channel<int16_t, 100, 1> ambientTemp = (int16_t)0;
	// deg C
	// offset 576
	scaled_channel<int16_t, 100, 1> compressorDischargeTemp = (int16_t)0;
	// kPa
	// offset 578
	scaled_channel<uint16_t, 30, 1> compressorDischargePressure = (uint16_t)0;
	// kPa
	// offset 580
	scaled_channel<uint16_t, 30, 1> throttleInletPressure = (uint16_t)0;
	// sec
	// offset 582
	uint16_t ignitionOnTime = (uint16_t)0;
	// sec
	// offset 584
	uint16_t engineRunTime = (uint16_t)0;
	// km
	// offset 586
	scaled_channel<uint16_t, 10, 1> distanceTraveled = (uint16_t)0;
	// Air/Fuel Ratio (Gas Scale)
	// AFR
	// offset 588
	scaled_channel<uint16_t, 1000, 1> afrGasolineScale = (uint16_t)0;
	// Air/Fuel Ratio 2 (Gas Scale)
	// AFR
	// offset 590
	scaled_channel<uint16_t, 1000, 1> afr2GasolineScale = (uint16_t)0;
	// Fuel: Last inj pulse width stg 2
	// ms
	// offset 592
	scaled_channel<uint16_t, 300, 1> actualLastInjectionStage2 = (uint16_t)0;
	// Fuel: injector duty cycle stage 2
	// %
	// offset 594
	scaled_channel<uint8_t, 2, 1> injectorDutyCycleStage2 = (uint8_t)0;
	// offset 595
	uint8_t pad = (uint8_t)0;
	// offset 596
	uint16_t mapAveragingSamples = (uint16_t)0;
	// ratio
	// offset 598
	scaled_channel<uint16_t, 1000, 1> dwellAccuracyRatio = (uint16_t)0;
	// MAF: Pre-filter
	// kg/h
	// offset 600
	scaled_channel<uint16_t, 10, 1> mafMeasured_preFilter = (uint16_t)0;
	// offset 602
	uint8_t alignmentFill_at_602[2];
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
static_assert(offsetof(output_channels_s, unused35) == 35);
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
static_assert(offsetof(output_channels_s, unused66) == 64);
static_assert(offsetof(output_channels_s, unused68) == 66);
static_assert(offsetof(output_channels_s, fuelTankLevel) == 68);
static_assert(offsetof(output_channels_s, totalFuelConsumption) == 70);
static_assert(offsetof(output_channels_s, fuelFlowRate) == 72);
static_assert(offsetof(output_channels_s, TPS2Value) == 74);
static_assert(offsetof(output_channels_s, seconds) == 76);
static_assert(offsetof(output_channels_s, engineMode) == 80);
static_assert(offsetof(output_channels_s, firmwareVersion) == 84);
static_assert(offsetof(output_channels_s, rawWastegatePosition) == 88);
static_assert(offsetof(output_channels_s, accelerationLat) == 90);
static_assert(offsetof(output_channels_s, accelerationLon) == 92);
static_assert(offsetof(output_channels_s, detectedGear) == 94);
static_assert(offsetof(output_channels_s, maxTriggerReentrant) == 95);
static_assert(offsetof(output_channels_s, rawLowFuelPressure) == 96);
static_assert(offsetof(output_channels_s, rawHighFuelPressure) == 98);
static_assert(offsetof(output_channels_s, lowFuelPressure) == 100);
static_assert(offsetof(output_channels_s, highFuelPressure) == 102);
static_assert(offsetof(output_channels_s, rawPpsSecondary) == 104);
static_assert(offsetof(output_channels_s, tcuDesiredGear) == 106);
static_assert(offsetof(output_channels_s, flexPercent) == 107);
static_assert(offsetof(output_channels_s, wastegatePositionSensor) == 108);
static_assert(offsetof(output_channels_s, wheelSpeedLf) == 110);
static_assert(offsetof(output_channels_s, wheelSpeedRf) == 111);
static_assert(offsetof(output_channels_s, calibrationValue) == 112);
static_assert(offsetof(output_channels_s, calibrationMode) == 116);
static_assert(offsetof(output_channels_s, idleStepperTargetPosition) == 117);
static_assert(offsetof(output_channels_s, wheelSpeedLr) == 118);
static_assert(offsetof(output_channels_s, wheelSpeedRr) == 119);
static_assert(offsetof(output_channels_s, orderingErrorCounter) == 120);
static_assert(offsetof(output_channels_s, tsConfigVersion) == 124);
static_assert(offsetof(output_channels_s, warningCounter) == 128);
static_assert(offsetof(output_channels_s, lastErrorCode) == 130);
static_assert(offsetof(output_channels_s, recentErrorCode) == 132);
static_assert(offsetof(output_channels_s, debugFloatField1) == 148);
static_assert(offsetof(output_channels_s, debugFloatField2) == 152);
static_assert(offsetof(output_channels_s, debugFloatField3) == 156);
static_assert(offsetof(output_channels_s, debugFloatField4) == 160);
static_assert(offsetof(output_channels_s, debugFloatField5) == 164);
static_assert(offsetof(output_channels_s, debugFloatField6) == 168);
static_assert(offsetof(output_channels_s, debugFloatField7) == 172);
static_assert(offsetof(output_channels_s, debugIntField1) == 176);
static_assert(offsetof(output_channels_s, debugIntField2) == 180);
static_assert(offsetof(output_channels_s, debugIntField3) == 184);
static_assert(offsetof(output_channels_s, debugIntField4) == 188);
static_assert(offsetof(output_channels_s, debugIntField5) == 190);
static_assert(offsetof(output_channels_s, egt) == 192);
static_assert(offsetof(output_channels_s, rawTps1Primary) == 208);
static_assert(offsetof(output_channels_s, rawPpsPrimary) == 210);
static_assert(offsetof(output_channels_s, rawClt) == 212);
static_assert(offsetof(output_channels_s, rawIat) == 214);
static_assert(offsetof(output_channels_s, rawOilPressure) == 216);
static_assert(offsetof(output_channels_s, fuelClosedLoopBinIdx) == 218);
static_assert(offsetof(output_channels_s, tcuCurrentGear) == 219);
static_assert(offsetof(output_channels_s, AFRValue) == 220);
static_assert(offsetof(output_channels_s, VssAcceleration) == 222);
static_assert(offsetof(output_channels_s, lambdaValues) == 224);
static_assert(offsetof(output_channels_s, AFRValue2) == 232);
static_assert(offsetof(output_channels_s, vvtPositionB1E) == 234);
static_assert(offsetof(output_channels_s, vvtPositionB2I) == 236);
static_assert(offsetof(output_channels_s, vvtPositionB2E) == 238);
static_assert(offsetof(output_channels_s, fuelPidCorrection) == 240);
static_assert(offsetof(output_channels_s, rawTps1Secondary) == 248);
static_assert(offsetof(output_channels_s, rawTps2Primary) == 250);
static_assert(offsetof(output_channels_s, rawTps2Secondary) == 252);
static_assert(offsetof(output_channels_s, accelerationVert) == 254);
static_assert(offsetof(output_channels_s, gyroYaw) == 256);
static_assert(offsetof(output_channels_s, turboSpeed) == 258);
static_assert(offsetof(output_channels_s, ignitionAdvanceCyl) == 260);
static_assert(offsetof(output_channels_s, tps1Split) == 284);
static_assert(offsetof(output_channels_s, tps2Split) == 286);
static_assert(offsetof(output_channels_s, tps12Split) == 288);
static_assert(offsetof(output_channels_s, accPedalSplit) == 290);
static_assert(offsetof(output_channels_s, sparkCutReason) == 292);
static_assert(offsetof(output_channels_s, fuelCutReason) == 293);
static_assert(offsetof(output_channels_s, instantRpm) == 294);
static_assert(offsetof(output_channels_s, rawMap) == 296);
static_assert(offsetof(output_channels_s, rawAfr) == 298);
static_assert(offsetof(output_channels_s, rawFuelTankLevel) == 300);
static_assert(offsetof(output_channels_s, calibrationValue2) == 304);
static_assert(offsetof(output_channels_s, luaInvocationCounter) == 308);
static_assert(offsetof(output_channels_s, luaLastCycleDuration) == 312);
static_assert(offsetof(output_channels_s, tcu_currentRange) == 316);
static_assert(offsetof(output_channels_s, tcRatio) == 318);
static_assert(offsetof(output_channels_s, vssEdgeCounter) == 320);
static_assert(offsetof(output_channels_s, issEdgeCounter) == 324);
static_assert(offsetof(output_channels_s, auxLinear1) == 328);
static_assert(offsetof(output_channels_s, auxLinear2) == 332);
static_assert(offsetof(output_channels_s, auxLinear3) == 336);
static_assert(offsetof(output_channels_s, auxLinear4) == 340);
static_assert(offsetof(output_channels_s, fallbackMap) == 344);
static_assert(offsetof(output_channels_s, instantMAPValue) == 346);
static_assert(offsetof(output_channels_s, maxLockedDuration) == 348);
static_assert(offsetof(output_channels_s, canWriteOk) == 350);
static_assert(offsetof(output_channels_s, canWriteNotOk) == 352);
static_assert(offsetof(output_channels_s, starterState) == 354);
static_assert(offsetof(output_channels_s, starterRelayDisable) == 355);
static_assert(offsetof(output_channels_s, multiSparkCounter) == 356);
static_assert(offsetof(output_channels_s, extiOverflowCount) == 357);
static_assert(offsetof(output_channels_s, auxSpeed1) == 420);
static_assert(offsetof(output_channels_s, auxSpeed2) == 422);
static_assert(offsetof(output_channels_s, ISSValue) == 424);
static_assert(offsetof(output_channels_s, rawAnalogInput) == 426);
static_assert(offsetof(output_channels_s, gppwmOutput) == 442);
static_assert(offsetof(output_channels_s, gppwmXAxis) == 446);
static_assert(offsetof(output_channels_s, gppwmYAxis) == 454);
static_assert(offsetof(output_channels_s, rawBattery) == 462);
static_assert(offsetof(output_channels_s, ignBlendParameter) == 464);
static_assert(offsetof(output_channels_s, ignBlendBias) == 472);
static_assert(offsetof(output_channels_s, ignBlendOutput) == 476);
static_assert(offsetof(output_channels_s, ignBlendYAxis) == 484);
static_assert(offsetof(output_channels_s, veBlendParameter) == 492);
static_assert(offsetof(output_channels_s, veBlendBias) == 500);
static_assert(offsetof(output_channels_s, veBlendOutput) == 504);
static_assert(offsetof(output_channels_s, veBlendYAxis) == 512);
static_assert(offsetof(output_channels_s, boostOpenLoopBlendParameter) == 520);
static_assert(offsetof(output_channels_s, boostOpenLoopBlendBias) == 524);
static_assert(offsetof(output_channels_s, boostOpenLoopBlendOutput) == 526);
static_assert(offsetof(output_channels_s, boostOpenLoopBlendYAxis) == 528);
static_assert(offsetof(output_channels_s, boostClosedLoopBlendParameter) == 532);
static_assert(offsetof(output_channels_s, boostClosedLoopBlendBias) == 536);
static_assert(offsetof(output_channels_s, boostClosedLoopBlendOutput) == 538);
static_assert(offsetof(output_channels_s, boostClosedLoopBlendYAxis) == 542);
static_assert(offsetof(output_channels_s, mapFast) == 546);
static_assert(offsetof(output_channels_s, outputRequestPeriod) == 548);
static_assert(offsetof(output_channels_s, luaGauges) == 552);
static_assert(offsetof(output_channels_s, rawMaf2) == 560);
static_assert(offsetof(output_channels_s, mafMeasured2) == 562);
static_assert(offsetof(output_channels_s, schedulingUsedCount) == 564);
static_assert(offsetof(output_channels_s, Gego) == 566);
static_assert(offsetof(output_channels_s, testBenchIter) == 568);
static_assert(offsetof(output_channels_s, oilTemp) == 570);
static_assert(offsetof(output_channels_s, fuelTemp) == 572);
static_assert(offsetof(output_channels_s, ambientTemp) == 574);
static_assert(offsetof(output_channels_s, compressorDischargeTemp) == 576);
static_assert(offsetof(output_channels_s, compressorDischargePressure) == 578);
static_assert(offsetof(output_channels_s, throttleInletPressure) == 580);
static_assert(offsetof(output_channels_s, ignitionOnTime) == 582);
static_assert(offsetof(output_channels_s, engineRunTime) == 584);
static_assert(offsetof(output_channels_s, distanceTraveled) == 586);
static_assert(offsetof(output_channels_s, afrGasolineScale) == 588);
static_assert(offsetof(output_channels_s, afr2GasolineScale) == 590);
static_assert(offsetof(output_channels_s, actualLastInjectionStage2) == 592);
static_assert(offsetof(output_channels_s, injectorDutyCycleStage2) == 594);
static_assert(offsetof(output_channels_s, pad) == 595);
static_assert(offsetof(output_channels_s, mapAveragingSamples) == 596);
static_assert(offsetof(output_channels_s, dwellAccuracyRatio) == 598);
static_assert(offsetof(output_channels_s, mafMeasured_preFilter) == 600);

