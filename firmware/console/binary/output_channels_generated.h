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
	// CLT
	// deg C
	// offset 8
	scaled_channel<int16_t, 100, 1> coolant = (int16_t)0;
	// IAT
	// deg C
	// offset 10
	scaled_channel<int16_t, 100, 1> intake = (int16_t)0;
	// Aux temp 1
	// deg C
	// offset 12
	scaled_channel<int16_t, 100, 1> auxTemp1 = (int16_t)0;
	// Aux temp 2
	// deg C
	// offset 14
	scaled_channel<int16_t, 100, 1> auxTemp2 = (int16_t)0;
	// deg C
	// offset 16
	scaled_channel<int16_t, 100, 1> oilTemp = (int16_t)0;
	// deg C
	// offset 18
	scaled_channel<int16_t, 100, 1> fuelTemp = (int16_t)0;
	// deg C
	// offset 20
	scaled_channel<int16_t, 100, 1> ambientTemp = (int16_t)0;
	// deg C
	// offset 22
	scaled_channel<int16_t, 100, 1> compressorDischargeTemp = (int16_t)0;
	// TPS
	// %
	// offset 24
	scaled_channel<int16_t, 100, 1> TPSValue = (int16_t)0;
	// ADC
	// offset 26
	uint16_t tpsADC = (uint16_t)0;
	// TPS2
	// %
	// offset 28
	scaled_channel<int16_t, 100, 1> TPS2Value = (int16_t)0;
	// Throttle pedal position
	// %
	// offset 30
	scaled_channel<int16_t, 100, 1> throttlePedalPosition = (int16_t)0;
	// %
	// offset 32
	scaled_channel<int16_t, 100, 1> tps1Split = (int16_t)0;
	// %
	// offset 34
	scaled_channel<int16_t, 100, 1> tps2Split = (int16_t)0;
	// %
	// offset 36
	scaled_channel<int16_t, 100, 1> tps12Split = (int16_t)0;
	// %
	// offset 38
	scaled_channel<int16_t, 100, 1> accPedalSplit = (int16_t)0;
	// Raw: TPS 1 primary
	// V
	// offset 40
	scaled_channel<int16_t, 1000, 1> rawTps1Primary = (int16_t)0;
	// Raw: TPS 1 secondary
	// V
	// offset 42
	scaled_channel<int16_t, 1000, 1> rawTps1Secondary = (int16_t)0;
	// Raw: TPS 2 primary
	// V
	// offset 44
	scaled_channel<int16_t, 1000, 1> rawTps2Primary = (int16_t)0;
	// Raw: TPS 2 secondary
	// V
	// offset 46
	scaled_channel<int16_t, 1000, 1> rawTps2Secondary = (int16_t)0;
	// Raw: Pedal primary
	// V
	// offset 48
	scaled_channel<int16_t, 1000, 1> rawPpsPrimary = (int16_t)0;
	// Raw: Pedal secondary
	// V
	// offset 50
	scaled_channel<int16_t, 1000, 1> rawPpsSecondary = (int16_t)0;
	// Raw: Vbatt
	// V
	// offset 52
	scaled_channel<int16_t, 1000, 1> rawBattery = (int16_t)0;
	// Raw: CLT
	// V
	// offset 54
	scaled_channel<int16_t, 1000, 1> rawClt = (int16_t)0;
	// Raw: IAT
	// V
	// offset 56
	scaled_channel<int16_t, 1000, 1> rawIat = (int16_t)0;
	// Raw: OilP
	// V
	// offset 58
	scaled_channel<int16_t, 1000, 1> rawOilPressure = (int16_t)0;
	// Raw: Fuel press low
	// V
	// offset 60
	scaled_channel<int16_t, 1000, 1> rawLowFuelPressure = (int16_t)0;
	// Raw: Fuel press high
	// V
	// offset 62
	scaled_channel<int16_t, 1000, 1> rawHighFuelPressure = (int16_t)0;
	// Raw: MAF
	// V
	// offset 64
	scaled_channel<uint16_t, 1000, 1> rawMaf = (uint16_t)0;
	// Raw: MAF 2
	// V
	// offset 66
	scaled_channel<uint16_t, 1000, 1> rawMaf2 = (uint16_t)0;
	// Raw: MAP
	// V
	// offset 68
	scaled_channel<uint16_t, 1000, 1> rawMap = (uint16_t)0;
	// Raw: Wastegate position
	// V
	// offset 70
	scaled_channel<int16_t, 1000, 1> rawWastegatePosition = (int16_t)0;
	// Raw: Fuel level
	// V
	// offset 72
	scaled_channel<uint16_t, 1000, 1> rawFuelTankLevel = (uint16_t)0;
	// Raw: AFR
	// V
	// offset 74
	scaled_channel<uint16_t, 1000, 1> rawAfr = (uint16_t)0;
	// V
	// offset 76
	scaled_channel<int16_t, 1000, 1> rawAnalogInput[8];
	// Lambda
	// offset 92
	scaled_channel<uint16_t, 10000, 1> lambdaValues[4];
	// Air/Fuel Ratio
	// AFR
	// offset 100
	scaled_channel<uint16_t, 1000, 1> AFRValue = (uint16_t)0;
	// Air/Fuel Ratio 2
	// AFR
	// offset 102
	scaled_channel<uint16_t, 1000, 1> AFRValue2 = (uint16_t)0;
	// Air/Fuel Ratio (Gas Scale)
	// AFR
	// offset 104
	scaled_channel<uint16_t, 1000, 1> afrGasolineScale = (uint16_t)0;
	// Air/Fuel Ratio 2 (Gas Scale)
	// AFR
	// offset 106
	scaled_channel<uint16_t, 1000, 1> afr2GasolineScale = (uint16_t)0;
	// Fuel pressure (low)
	// kpa
	// offset 108
	scaled_channel<int16_t, 30, 1> lowFuelPressure = (int16_t)0;
	// Fuel pressure (high)
	// bar
	// offset 110
	scaled_channel<int16_t, 10, 1> highFuelPressure = (int16_t)0;
	// Fuel level
	// %
	// offset 112
	scaled_channel<int16_t, 100, 1> fuelTankLevel = (int16_t)0;
	// Flex Ethanol %
	// %
	// offset 114
	scaled_channel<uint8_t, 2, 1> flexPercent = (uint8_t)0;
	// Vehicle Speed
	// kph
	// offset 115
	uint8_t vehicleSpeedKph = (uint8_t)0;
	// Gearbox Ratio
	// value
	// offset 116
	scaled_channel<uint16_t, 100, 1> speedToRpmRatio = (uint16_t)0;
	// Wheel speed: LF
	// offset 118
	uint8_t wheelSpeedLf = (uint8_t)0;
	// Wheel speed: RF
	// offset 119
	uint8_t wheelSpeedRf = (uint8_t)0;
	// Wheel speed: LR
	// offset 120
	uint8_t wheelSpeedLr = (uint8_t)0;
	// Wheel speed: RR
	// offset 121
	uint8_t wheelSpeedRr = (uint8_t)0;
	// VVT: bank 1 intake
	// deg
	// offset 122
	scaled_channel<int16_t, 50, 1> vvtPositionB1I = (int16_t)0;
	// offset 124
	uint32_t tsConfigVersion = (uint32_t)0;
	// VVT: bank 1 exhaust
	// deg
	// offset 128
	scaled_channel<int16_t, 50, 1> vvtPositionB1E = (int16_t)0;
	// VVT: bank 2 intake
	// deg
	// offset 130
	scaled_channel<int16_t, 50, 1> vvtPositionB2I = (int16_t)0;
	// VVT: bank 2 exhaust
	// deg
	// offset 132
	scaled_channel<int16_t, 50, 1> vvtPositionB2E = (int16_t)0;
	// kPa
	// offset 134
	scaled_channel<uint16_t, 30, 1> baroPressure = (uint16_t)0;
	// MAP
	// kPa
	// offset 136
	scaled_channel<uint16_t, 30, 1> MAPValue = (uint16_t)0;
	// kPa
	// offset 138
	scaled_channel<uint16_t, 30, 1> mapFast = (uint16_t)0;
	// Oil Pressure
	// kPa
	// offset 140
	scaled_channel<uint16_t, 30, 1> oilPressure = (uint16_t)0;
	// kPa
	// offset 142
	scaled_channel<uint16_t, 30, 1> compressorDischargePressure = (uint16_t)0;
	// kPa
	// offset 144
	scaled_channel<uint16_t, 30, 1> throttleInletPressure = (uint16_t)0;
	// Aux linear 1
	// offset 146
	scaled_channel<int16_t, 10, 1> auxLinear1 = (int16_t)0;
	// Aux linear 2
	// offset 148
	scaled_channel<int16_t, 10, 1> auxLinear2 = (int16_t)0;
	// Aux linear 3
	// offset 150
	scaled_channel<int16_t, 10, 1> auxLinear3 = (int16_t)0;
	// Aux linear 4
	// offset 152
	scaled_channel<int16_t, 10, 1> auxLinear4 = (int16_t)0;
	// VBatt
	// V
	// offset 154
	scaled_channel<uint16_t, 1000, 1> VBatt = (uint16_t)0;
	// Wastegate position sensor
	// %
	// offset 156
	scaled_channel<int16_t, 100, 1> wastegatePositionSensor = (int16_t)0;
	// Aux speed 1
	// s
	// offset 158
	uint16_t auxSpeed1 = (uint16_t)0;
	// Aux speed 2
	// s
	// offset 160
	uint16_t auxSpeed2 = (uint16_t)0;
	// MAF
	// kg/h
	// offset 162
	scaled_channel<uint16_t, 10, 1> mafMeasured = (uint16_t)0;
	// MAF #2
	// kg/h
	// offset 164
	scaled_channel<uint16_t, 10, 1> mafMeasured2 = (uint16_t)0;
	// Fuel: Trim bank
	// %
	// offset 166
	scaled_channel<int16_t, 100, 1> fuelPidCorrection[4];
	// %
	// offset 174
	scaled_channel<uint16_t, 100, 1> Gego = (uint16_t)0;
	// Fuel: Flow rate
	// gram/s
	// offset 176
	scaled_channel<uint16_t, 200, 1> fuelFlowRate = (uint16_t)0;
	// Fuel: Total consumed
	// grams
	// offset 178
	uint16_t totalFuelConsumption = (uint16_t)0;
	// sec
	// offset 180
	uint16_t ignitionOnTime = (uint16_t)0;
	// sec
	// offset 182
	uint16_t engineRunTime = (uint16_t)0;
	// km
	// offset 184
	scaled_channel<uint16_t, 10, 1> distanceTraveled = (uint16_t)0;
	// Fuel: Wall amount
	// mg
	// offset 186
	scaled_channel<uint16_t, 100, 1> wallFuelAmount = (uint16_t)0;
	// Fuel: Wall correction
	// mg
	// offset 188
	scaled_channel<int16_t, 100, 1> wallFuelCorrectionValue = (int16_t)0;
	// Fuel: VE
	// ratio
	// offset 190
	scaled_channel<uint8_t, 2, 1> veValue = (uint8_t)0;
	// Detected Gear
	// offset 191
	uint8_t detectedGear = (uint8_t)0;
	// offset 192
	uint8_t maxTriggerReentrant = (uint8_t)0;
	// offset 193
	uint8_t alignmentFill_at_193[3];
	// Lua: Gauge
	// value
	// offset 196
	float luaGauges[2];
	// Lua: Last tick duration
	// us
	// offset 204
	uint16_t luaLastCycleDuration = (uint16_t)0;
	// Lua: Tick counter
	// count
	// offset 206
	uint8_t luaInvocationCounter = (uint8_t)0;
	// %
	// offset 207
	uint8_t widebandUpdateProgress = (uint8_t)0;
	// offset 208
	uint8_t orderingErrorCounter = (uint8_t)0;
	// ECU temperature
	// deg C
	// offset 209
	int8_t internalMcuTemperature = (int8_t)0;
	// Fuel: Last inj pulse width
	// ms
	// offset 210
	scaled_channel<uint16_t, 300, 1> actualLastInjection = (uint16_t)0;
	// Fuel: Last inj pulse width stg 2
	// ms
	// offset 212
	scaled_channel<uint16_t, 300, 1> actualLastInjectionStage2 = (uint16_t)0;
	// Fuel: injector duty cycle
	// %
	// offset 214
	scaled_channel<uint8_t, 2, 1> injectorDutyCycle = (uint8_t)0;
	// Fuel: injector duty cycle stage 2
	// %
	// offset 215
	scaled_channel<uint8_t, 2, 1> injectorDutyCycleStage2 = (uint8_t)0;
	// Fuel: Injection timing SOI
	// deg
	// offset 216
	int16_t injectionOffset = (int16_t)0;
	// Trg: Revolution counter
	// offset 218
	uint16_t revolutionCounterSinceStart = (uint16_t)0;
	// CAN: Rx
	// offset 220
	uint16_t canReadCounter = (uint16_t)0;
	// Fuel: TPS AE add fuel ms
	// ms
	// offset 222
	scaled_channel<int16_t, 300, 1> tpsAccelFuel = (int16_t)0;
	// Ign: Timing Base
	// deg
	// offset 224
	scaled_channel<int16_t, 50, 1> ignitionAdvance = (int16_t)0;
	// Ign: Mode
	// offset 226
	uint8_t currentIgnitionMode = (uint8_t)0;
	// Fuel: Injection mode
	// offset 227
	uint8_t currentInjectionMode = (uint8_t)0;
	// Idle: Stepper target position
	// offset 228
	uint8_t idleStepperTargetPosition = (uint8_t)0;
	// offset 229
	uint8_t alignmentFill_at_229[1];
	// Ign: Coil duty cycle
	// %
	// offset 230
	scaled_channel<uint16_t, 100, 1> coilDutyCycle = (uint16_t)0;
	// Uptime
	// sec
	// offset 232
	uint32_t seconds = (uint32_t)0;
	// firmware
	// version_f
	// offset 236
	uint32_t firmwareVersion = (uint32_t)0;
	// Accel: Lateral
	// G
	// offset 240
	scaled_channel<int16_t, 1000, 1> accelerationLat = (int16_t)0;
	// Accel: Longitudinal
	// G
	// offset 242
	scaled_channel<int16_t, 1000, 1> accelerationLon = (int16_t)0;
	// offset 244
	float calibrationValue = (float)0;
	// offset 248
	float calibrationValue2 = (float)0;
	// offset 252
	uint8_t calibrationMode = (uint8_t)0;
	// offset 253
	uint8_t schedulingUsedCount = (uint8_t)0;
	// Warning: counter
	// count
	// offset 254
	uint16_t warningCounter = (uint16_t)0;
	// Warning: last
	// error
	// offset 256
	uint16_t lastErrorCode = (uint16_t)0;
	// error
	// offset 258
	uint16_t recentErrorCode[8];
	// offset 274
	uint8_t alignmentFill_at_274[2];
	// val
	// offset 276
	float debugFloatField1 = (float)0;
	// val
	// offset 280
	float debugFloatField2 = (float)0;
	// val
	// offset 284
	float debugFloatField3 = (float)0;
	// val
	// offset 288
	float debugFloatField4 = (float)0;
	// val
	// offset 292
	float debugFloatField5 = (float)0;
	// val
	// offset 296
	float debugFloatField6 = (float)0;
	// val
	// offset 300
	float debugFloatField7 = (float)0;
	// val
	// offset 304
	uint32_t debugIntField1 = (uint32_t)0;
	// val
	// offset 308
	uint32_t debugIntField2 = (uint32_t)0;
	// val
	// offset 312
	uint32_t debugIntField3 = (uint32_t)0;
	// val
	// offset 316
	int16_t debugIntField4 = (int16_t)0;
	// val
	// offset 318
	int16_t debugIntField5 = (int16_t)0;
	// EGT
	// deg C
	// offset 320
	uint16_t egt[8];
	// offset 336
	uint8_t fuelClosedLoopBinIdx = (uint8_t)0;
	// offset 337
	uint8_t alignmentFill_at_337[1];
	// Accel: Vertical
	// G
	// offset 338
	scaled_channel<int16_t, 1000, 1> accelerationVert = (int16_t)0;
	// Gyro: Yaw rate
	// deg/sec
	// offset 340
	scaled_channel<int16_t, 1000, 1> gyroYaw = (int16_t)0;
	// Turbocharger Speed
	// hz
	// offset 342
	uint16_t turboSpeed = (uint16_t)0;
	// Ign: Timing Cyl
	// deg
	// offset 344
	scaled_channel<int16_t, 50, 1> ignitionAdvanceCyl[12];
	// Ign: Cut Code
	// code
	// offset 368
	int8_t sparkCutReason = (int8_t)0;
	// Fuel: Cut Code
	// code
	// offset 369
	int8_t fuelCutReason = (int8_t)0;
	// rpm
	// offset 370
	uint16_t instantRpm = (uint16_t)0;
	// count
	// offset 372
	uint16_t testBenchIter = (uint16_t)0;
	// offset 374
	uint8_t vssEdgeCounter = (uint8_t)0;
	// offset 375
	uint8_t alignmentFill_at_375[1];
	// kPa
	// offset 376
	scaled_channel<uint16_t, 10, 1> fallbackMap = (uint16_t)0;
	// Instant MAP
	// kPa
	// offset 378
	scaled_channel<uint16_t, 30, 1> instantMAPValue = (uint16_t)0;
	// us
	// offset 380
	uint16_t maxLockedDuration = (uint16_t)0;
	// CAN: Tx OK
	// offset 382
	uint16_t canWriteOk = (uint16_t)0;
	// CAN: Tx err
	// offset 384
	uint16_t canWriteNotOk = (uint16_t)0;
	// offset 386
	uint8_t starterState = (uint8_t)0;
	// offset 387
	uint8_t starterRelayDisable = (uint8_t)0;
	// Ign: Multispark count
	// offset 388
	uint8_t multiSparkCounter = (uint8_t)0;
	// offset 389
	uint8_t extiOverflowCount = (uint8_t)0;
	// offset 390
	uint8_t alignmentFill_at_390[2];
	// offset 392
	pid_status_s alternatorStatus;
	// offset 404
	pid_status_s idleStatus;
	// offset 416
	pid_status_s etbStatus;
	// offset 428
	pid_status_s boostStatus;
	// offset 440
	pid_status_s wastegateDcStatus;
	// GPPWM Output
	// %
	// offset 452
	scaled_channel<uint8_t, 2, 1> gppwmOutput[4];
	// offset 456
	int16_t gppwmXAxis[4];
	// offset 464
	scaled_channel<int16_t, 10, 1> gppwmYAxis[4];
	// offset 472
	scaled_channel<int16_t, 10, 1> ignBlendParameter[4];
	// %
	// offset 480
	scaled_channel<uint8_t, 2, 1> ignBlendBias[4];
	// deg
	// offset 484
	scaled_channel<int16_t, 100, 1> ignBlendOutput[4];
	// offset 492
	scaled_channel<int16_t, 10, 1> ignBlendYAxis[4];
	// offset 500
	scaled_channel<int16_t, 10, 1> veBlendParameter[4];
	// %
	// offset 508
	scaled_channel<uint8_t, 2, 1> veBlendBias[4];
	// %
	// offset 512
	scaled_channel<int16_t, 100, 1> veBlendOutput[4];
	// offset 520
	scaled_channel<int16_t, 10, 1> veBlendYAxis[4];
	// offset 528
	uint16_t mapAveragingSamples = (uint16_t)0;
	// ratio
	// offset 530
	scaled_channel<uint16_t, 1000, 1> dwellAccuracyRatio = (uint16_t)0;
	// MAF: Pre-filter
	// kg/h
	// offset 532
	scaled_channel<uint16_t, 10, 1> mafMeasured_preFilter = (uint16_t)0;
	// rpm
	// offset 534
	uint16_t cylinderRpm[12];
	// rpm
	// offset 558
	int8_t cylinderRpmDelta[12];
	// offset 570
	uint8_t alignmentFill_at_570[2];
};
static_assert(sizeof(output_channels_s) == 572);
static_assert(offsetof(output_channels_s, RPMValue) == 4);
static_assert(offsetof(output_channels_s, rpmAcceleration) == 6);
static_assert(offsetof(output_channels_s, coolant) == 8);
static_assert(offsetof(output_channels_s, intake) == 10);
static_assert(offsetof(output_channels_s, auxTemp1) == 12);
static_assert(offsetof(output_channels_s, auxTemp2) == 14);
static_assert(offsetof(output_channels_s, oilTemp) == 16);
static_assert(offsetof(output_channels_s, fuelTemp) == 18);
static_assert(offsetof(output_channels_s, ambientTemp) == 20);
static_assert(offsetof(output_channels_s, compressorDischargeTemp) == 22);
static_assert(offsetof(output_channels_s, TPSValue) == 24);
static_assert(offsetof(output_channels_s, tpsADC) == 26);
static_assert(offsetof(output_channels_s, TPS2Value) == 28);
static_assert(offsetof(output_channels_s, throttlePedalPosition) == 30);
static_assert(offsetof(output_channels_s, tps1Split) == 32);
static_assert(offsetof(output_channels_s, tps2Split) == 34);
static_assert(offsetof(output_channels_s, tps12Split) == 36);
static_assert(offsetof(output_channels_s, accPedalSplit) == 38);
static_assert(offsetof(output_channels_s, rawTps1Primary) == 40);
static_assert(offsetof(output_channels_s, rawTps1Secondary) == 42);
static_assert(offsetof(output_channels_s, rawTps2Primary) == 44);
static_assert(offsetof(output_channels_s, rawTps2Secondary) == 46);
static_assert(offsetof(output_channels_s, rawPpsPrimary) == 48);
static_assert(offsetof(output_channels_s, rawPpsSecondary) == 50);
static_assert(offsetof(output_channels_s, rawBattery) == 52);
static_assert(offsetof(output_channels_s, rawClt) == 54);
static_assert(offsetof(output_channels_s, rawIat) == 56);
static_assert(offsetof(output_channels_s, rawOilPressure) == 58);
static_assert(offsetof(output_channels_s, rawLowFuelPressure) == 60);
static_assert(offsetof(output_channels_s, rawHighFuelPressure) == 62);
static_assert(offsetof(output_channels_s, rawMaf) == 64);
static_assert(offsetof(output_channels_s, rawMaf2) == 66);
static_assert(offsetof(output_channels_s, rawMap) == 68);
static_assert(offsetof(output_channels_s, rawWastegatePosition) == 70);
static_assert(offsetof(output_channels_s, rawFuelTankLevel) == 72);
static_assert(offsetof(output_channels_s, rawAfr) == 74);
static_assert(offsetof(output_channels_s, rawAnalogInput) == 76);
static_assert(offsetof(output_channels_s, lambdaValues) == 92);
static_assert(offsetof(output_channels_s, AFRValue) == 100);
static_assert(offsetof(output_channels_s, AFRValue2) == 102);
static_assert(offsetof(output_channels_s, afrGasolineScale) == 104);
static_assert(offsetof(output_channels_s, afr2GasolineScale) == 106);
static_assert(offsetof(output_channels_s, lowFuelPressure) == 108);
static_assert(offsetof(output_channels_s, highFuelPressure) == 110);
static_assert(offsetof(output_channels_s, fuelTankLevel) == 112);
static_assert(offsetof(output_channels_s, flexPercent) == 114);
static_assert(offsetof(output_channels_s, vehicleSpeedKph) == 115);
static_assert(offsetof(output_channels_s, speedToRpmRatio) == 116);
static_assert(offsetof(output_channels_s, wheelSpeedLf) == 118);
static_assert(offsetof(output_channels_s, wheelSpeedRf) == 119);
static_assert(offsetof(output_channels_s, wheelSpeedLr) == 120);
static_assert(offsetof(output_channels_s, wheelSpeedRr) == 121);
static_assert(offsetof(output_channels_s, vvtPositionB1I) == 122);
static_assert(offsetof(output_channels_s, tsConfigVersion) == 124);
static_assert(offsetof(output_channels_s, vvtPositionB1E) == 128);
static_assert(offsetof(output_channels_s, vvtPositionB2I) == 130);
static_assert(offsetof(output_channels_s, vvtPositionB2E) == 132);
static_assert(offsetof(output_channels_s, baroPressure) == 134);
static_assert(offsetof(output_channels_s, MAPValue) == 136);
static_assert(offsetof(output_channels_s, mapFast) == 138);
static_assert(offsetof(output_channels_s, oilPressure) == 140);
static_assert(offsetof(output_channels_s, compressorDischargePressure) == 142);
static_assert(offsetof(output_channels_s, throttleInletPressure) == 144);
static_assert(offsetof(output_channels_s, auxLinear1) == 146);
static_assert(offsetof(output_channels_s, auxLinear2) == 148);
static_assert(offsetof(output_channels_s, auxLinear3) == 150);
static_assert(offsetof(output_channels_s, auxLinear4) == 152);
static_assert(offsetof(output_channels_s, VBatt) == 154);
static_assert(offsetof(output_channels_s, wastegatePositionSensor) == 156);
static_assert(offsetof(output_channels_s, auxSpeed1) == 158);
static_assert(offsetof(output_channels_s, auxSpeed2) == 160);
static_assert(offsetof(output_channels_s, mafMeasured) == 162);
static_assert(offsetof(output_channels_s, mafMeasured2) == 164);
static_assert(offsetof(output_channels_s, fuelPidCorrection) == 166);
static_assert(offsetof(output_channels_s, Gego) == 174);
static_assert(offsetof(output_channels_s, fuelFlowRate) == 176);
static_assert(offsetof(output_channels_s, totalFuelConsumption) == 178);
static_assert(offsetof(output_channels_s, ignitionOnTime) == 180);
static_assert(offsetof(output_channels_s, engineRunTime) == 182);
static_assert(offsetof(output_channels_s, distanceTraveled) == 184);
static_assert(offsetof(output_channels_s, wallFuelAmount) == 186);
static_assert(offsetof(output_channels_s, wallFuelCorrectionValue) == 188);
static_assert(offsetof(output_channels_s, veValue) == 190);
static_assert(offsetof(output_channels_s, detectedGear) == 191);
static_assert(offsetof(output_channels_s, maxTriggerReentrant) == 192);
static_assert(offsetof(output_channels_s, luaGauges) == 196);
static_assert(offsetof(output_channels_s, luaLastCycleDuration) == 204);
static_assert(offsetof(output_channels_s, luaInvocationCounter) == 206);
static_assert(offsetof(output_channels_s, widebandUpdateProgress) == 207);
static_assert(offsetof(output_channels_s, orderingErrorCounter) == 208);
static_assert(offsetof(output_channels_s, internalMcuTemperature) == 209);
static_assert(offsetof(output_channels_s, actualLastInjection) == 210);
static_assert(offsetof(output_channels_s, actualLastInjectionStage2) == 212);
static_assert(offsetof(output_channels_s, injectorDutyCycle) == 214);
static_assert(offsetof(output_channels_s, injectorDutyCycleStage2) == 215);
static_assert(offsetof(output_channels_s, injectionOffset) == 216);
static_assert(offsetof(output_channels_s, revolutionCounterSinceStart) == 218);
static_assert(offsetof(output_channels_s, canReadCounter) == 220);
static_assert(offsetof(output_channels_s, tpsAccelFuel) == 222);
static_assert(offsetof(output_channels_s, ignitionAdvance) == 224);
static_assert(offsetof(output_channels_s, currentIgnitionMode) == 226);
static_assert(offsetof(output_channels_s, currentInjectionMode) == 227);
static_assert(offsetof(output_channels_s, idleStepperTargetPosition) == 228);
static_assert(offsetof(output_channels_s, coilDutyCycle) == 230);
static_assert(offsetof(output_channels_s, seconds) == 232);
static_assert(offsetof(output_channels_s, firmwareVersion) == 236);
static_assert(offsetof(output_channels_s, accelerationLat) == 240);
static_assert(offsetof(output_channels_s, accelerationLon) == 242);
static_assert(offsetof(output_channels_s, calibrationValue) == 244);
static_assert(offsetof(output_channels_s, calibrationValue2) == 248);
static_assert(offsetof(output_channels_s, calibrationMode) == 252);
static_assert(offsetof(output_channels_s, schedulingUsedCount) == 253);
static_assert(offsetof(output_channels_s, warningCounter) == 254);
static_assert(offsetof(output_channels_s, lastErrorCode) == 256);
static_assert(offsetof(output_channels_s, recentErrorCode) == 258);
static_assert(offsetof(output_channels_s, debugFloatField1) == 276);
static_assert(offsetof(output_channels_s, debugFloatField2) == 280);
static_assert(offsetof(output_channels_s, debugFloatField3) == 284);
static_assert(offsetof(output_channels_s, debugFloatField4) == 288);
static_assert(offsetof(output_channels_s, debugFloatField5) == 292);
static_assert(offsetof(output_channels_s, debugFloatField6) == 296);
static_assert(offsetof(output_channels_s, debugFloatField7) == 300);
static_assert(offsetof(output_channels_s, debugIntField1) == 304);
static_assert(offsetof(output_channels_s, debugIntField2) == 308);
static_assert(offsetof(output_channels_s, debugIntField3) == 312);
static_assert(offsetof(output_channels_s, debugIntField4) == 316);
static_assert(offsetof(output_channels_s, debugIntField5) == 318);
static_assert(offsetof(output_channels_s, egt) == 320);
static_assert(offsetof(output_channels_s, fuelClosedLoopBinIdx) == 336);
static_assert(offsetof(output_channels_s, accelerationVert) == 338);
static_assert(offsetof(output_channels_s, gyroYaw) == 340);
static_assert(offsetof(output_channels_s, turboSpeed) == 342);
static_assert(offsetof(output_channels_s, ignitionAdvanceCyl) == 344);
static_assert(offsetof(output_channels_s, sparkCutReason) == 368);
static_assert(offsetof(output_channels_s, fuelCutReason) == 369);
static_assert(offsetof(output_channels_s, instantRpm) == 370);
static_assert(offsetof(output_channels_s, testBenchIter) == 372);
static_assert(offsetof(output_channels_s, vssEdgeCounter) == 374);
static_assert(offsetof(output_channels_s, fallbackMap) == 376);
static_assert(offsetof(output_channels_s, instantMAPValue) == 378);
static_assert(offsetof(output_channels_s, maxLockedDuration) == 380);
static_assert(offsetof(output_channels_s, canWriteOk) == 382);
static_assert(offsetof(output_channels_s, canWriteNotOk) == 384);
static_assert(offsetof(output_channels_s, starterState) == 386);
static_assert(offsetof(output_channels_s, starterRelayDisable) == 387);
static_assert(offsetof(output_channels_s, multiSparkCounter) == 388);
static_assert(offsetof(output_channels_s, extiOverflowCount) == 389);
static_assert(offsetof(output_channels_s, gppwmOutput) == 452);
static_assert(offsetof(output_channels_s, gppwmXAxis) == 456);
static_assert(offsetof(output_channels_s, gppwmYAxis) == 464);
static_assert(offsetof(output_channels_s, ignBlendParameter) == 472);
static_assert(offsetof(output_channels_s, ignBlendBias) == 480);
static_assert(offsetof(output_channels_s, ignBlendOutput) == 484);
static_assert(offsetof(output_channels_s, ignBlendYAxis) == 492);
static_assert(offsetof(output_channels_s, veBlendParameter) == 500);
static_assert(offsetof(output_channels_s, veBlendBias) == 508);
static_assert(offsetof(output_channels_s, veBlendOutput) == 512);
static_assert(offsetof(output_channels_s, veBlendYAxis) == 520);
static_assert(offsetof(output_channels_s, mapAveragingSamples) == 528);
static_assert(offsetof(output_channels_s, dwellAccuracyRatio) == 530);
static_assert(offsetof(output_channels_s, mafMeasured_preFilter) == 532);
static_assert(offsetof(output_channels_s, cylinderRpm) == 534);
static_assert(offsetof(output_channels_s, cylinderRpmDelta) == 558);

