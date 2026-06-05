#include "pch.h"

#include "malfunction_central.h"

static bool hasError(ObdCode code) {
	error_codes_set_s errors;
	getErrorCodes(&errors);

	for (int i = 0; i < errors.count; i++) {
		if (errors.error_codes[i] == code) {
			return true;
		}
	}

	return false;
}

// Set up conditions so that the sensor checker won't bail out early:
// - time > 1 second (startup inhibit)
// - battery voltage sufficient
// - ignition on for > 5 seconds
// - crank synced with > 20 sync events and a recent trigger event
static void setupSensorCheckerPreconditions() {
	// Clear any errors from previous tests
	clearWarnings();

	// Battery voltage above 7V threshold
	Sensor::setMockValue(SensorType::BatteryVoltage, 12.0f);

	// Turn ignition on
	engine->module<SensorChecker>()->onIgnitionStateChanged(true);

	// Advance time past the 1s startup inhibit + 5s stabilization
	advanceTimeUs(10e6);

	// Simulate crank sync: enough sync events and a recent trigger event
	auto& triggerState = engine->triggerCentral.triggerState;
	triggerState.crankSynchronizationCounter = 25;
	triggerState.hasSignal = true;

	// Simulate engine running
	engine->rpmCalculator.setRpmValue(2000);
}

// Helper: set up a cam decoder that looks fully healthy
static void setupHealthyCam(int bank, int cam) {
	auto& vvtDecoder = engine->triggerCentral.vvtState[bank][cam];
	vvtDecoder.hasSignal = true;
	vvtDecoder.triggerErrorCounter = 0;
	engine->triggerCentral.vvtPosition[bank][cam].t.reset();
}

// ==================== checkTriggerDecoder (crank) ====================

TEST(SensorCheckerCrank, NoErrorWhenFewSyncErrors) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setupSensorCheckerPreconditions();

	engine->triggerCentral.triggerState.triggerErrorCounter = 10;

	engine->module<SensorChecker>()->onSlowCallback();

	EXPECT_FALSE(hasError(ObdCode::OBD_Crankshaft_Position_Sensor_A_Circuit_SyncErrors));
}

TEST(SensorCheckerCrank, ErrorWhenManySyncErrors) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setupSensorCheckerPreconditions();

	engine->triggerCentral.triggerState.triggerErrorCounter = 51;

	engine->module<SensorChecker>()->onSlowCallback();

	EXPECT_TRUE(hasError(ObdCode::OBD_Crankshaft_Position_Sensor_A_Circuit_SyncErrors));
}

// ==================== checkCamDecoder ====================

// No cam pin configured → no error even though cam state looks bad
TEST(SensorCheckerCam, NoPinConfiguredSkipsCheck) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	// camInputs[0] left as default (invalid pin)

	setupSensorCheckerPreconditions();

	// Cam has no signal, but check should be skipped entirely
	ASSERT_FALSE(engine->triggerCentral.vvtState[0][0].hasSignal);

	engine->module<SensorChecker>()->onSlowCallback();

	EXPECT_FALSE(hasError(ObdCode::OBD_Camshaft_Position_Sensor_B1I_NoSignal));
	EXPECT_FALSE(hasError(ObdCode::OBD_Camshaft_Position_Sensor_B1I_SyncErrors));
}

// Scenario 1: no cam edges at all → no-signal error
TEST(SensorCheckerCam, NoSignalWhenNoEdges) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->camInputs[0] = Gpio::A0;

	setupSensorCheckerPreconditions();

	ASSERT_FALSE(engine->triggerCentral.vvtState[0][0].hasSignal);

	engine->module<SensorChecker>()->onSlowCallback();

	EXPECT_TRUE(hasError(ObdCode::OBD_Camshaft_Position_Sensor_B1I_NoSignal));
}

// Scenario 2: cam edges present but VVT position has timed out → no-signal error
TEST(SensorCheckerCam, SignalButNoSync) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->camInputs[0] = Gpio::A0;

	setupSensorCheckerPreconditions();

	auto& vvtDecoder = engine->triggerCentral.vvtState[0][0];
	vvtDecoder.hasSignal = true;

	// Do NOT reset vvtPosition timer — it's stale (>1s), simulating no VVT sync
	engine->module<SensorChecker>()->onSlowCallback();

	EXPECT_TRUE(hasError(ObdCode::OBD_Camshaft_Position_Sensor_B1I_NoSignal));
}

// Scenario 3: cam has signal and sync, but too many trigger errors → sync-errors error
TEST(SensorCheckerCam, TooManySyncErrors) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->camInputs[0] = Gpio::A0;

	setupSensorCheckerPreconditions();
	setupHealthyCam(0, 0);

	// Inject many sync errors
	engine->triggerCentral.vvtState[0][0].triggerErrorCounter = 51;

	engine->module<SensorChecker>()->onSlowCallback();

	EXPECT_TRUE(hasError(ObdCode::OBD_Camshaft_Position_Sensor_B1I_SyncErrors));
}

// All good: cam has signal, recent VVT position, low error count → no errors
TEST(SensorCheckerCam, AllGoodNoErrors) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->camInputs[0] = Gpio::A0;

	setupSensorCheckerPreconditions();
	setupHealthyCam(0, 0);

	engine->module<SensorChecker>()->onSlowCallback();

	EXPECT_FALSE(hasError(ObdCode::OBD_Camshaft_Position_Sensor_B1I_NoSignal));
	EXPECT_FALSE(hasError(ObdCode::OBD_Camshaft_Position_Sensor_B1I_SyncErrors));
}

// hasSignal survives edgeCountRise rollover — the bug we fixed.
// Previously the check was `edgeCountRise == 0`, which would false-positive on rollover.
TEST(SensorCheckerCam, SignalSurvivesCounterRollover) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->camInputs[0] = Gpio::A0;

	setupSensorCheckerPreconditions();
	setupHealthyCam(0, 0);

	// Counter has rolled over to 0, but hasSignal is still true
	engine->triggerCentral.vvtState[0][0].edgeCountRise = 0;

	engine->module<SensorChecker>()->onSlowCallback();

	EXPECT_FALSE(hasError(ObdCode::OBD_Camshaft_Position_Sensor_B1I_NoSignal));
}

// ==================== Guard conditions ====================

// Cam checks should be skipped if engine hasn't moved recently
TEST(SensorCheckerCam, SkippedWhenEngineNotMoving) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->camInputs[0] = Gpio::A0;

	setupSensorCheckerPreconditions();

	// Stop the engine
	engine->rpmCalculator.setStopSpinning();

	ASSERT_FALSE(engine->triggerCentral.vvtState[0][0].hasSignal);

	engine->module<SensorChecker>()->onSlowCallback();

	// Cam no-signal should NOT be reported because engine isn't moving
	EXPECT_FALSE(hasError(ObdCode::OBD_Camshaft_Position_Sensor_B1I_NoSignal));
}

// Cam checks should be skipped if crank hasn't accumulated enough syncs
TEST(SensorCheckerCam, SkippedWhenNotEnoughCrankSyncs) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->camInputs[0] = Gpio::A0;

	setupSensorCheckerPreconditions();

	// Override: only a few crank syncs (threshold is > 20)
	engine->triggerCentral.triggerState.crankSynchronizationCounter = 5;

	ASSERT_FALSE(engine->triggerCentral.vvtState[0][0].hasSignal);

	engine->module<SensorChecker>()->onSlowCallback();

	// Cam no-signal should NOT be reported because crank hasn't synced enough
	EXPECT_FALSE(hasError(ObdCode::OBD_Camshaft_Position_Sensor_B1I_NoSignal));
}

// ==================== Spurious errors during state changes ====================
// These reproduce the conditions around shutdown / restart where the cam position
// has gone stale but the engine state means we must NOT raise a fault code.

// Shutdown: engine still "running" but coasting down below cranking RPM. The cam
// position legitimately goes stale faster than the engine turns at low RPM, so we
// must suppress the check rather than flag a spurious code on the way to stopping.
TEST(SensorCheckerCam, NoSpuriousNoSignalDuringSpinDown) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->camInputs[0] = Gpio::A0;
	engineConfiguration->cranking.rpm = 550;

	setupSensorCheckerPreconditions();
	// Cam has edges but a stale VVT position (scenario 2)
	engine->triggerCentral.vvtState[0][0].hasSignal = true;

	// Positive control: at running RPM the stale cam IS flagged
	engine->module<SensorChecker>()->onSlowCallback();
	ASSERT_TRUE(hasError(ObdCode::OBD_Camshaft_Position_Sensor_B1I_NoSignal));

	// Now coast down below cranking RPM (still RUNNING state, about to stop)
	clearWarnings();
	engine->rpmCalculator.setRpmValue(100);
	ASSERT_TRUE(engine->rpmCalculator.isRunning());

	engine->module<SensorChecker>()->onSlowCallback();
	EXPECT_FALSE(hasError(ObdCode::OBD_Camshaft_Position_Sensor_B1I_NoSignal));
}

// Restart: engine cranking again after a stop. The primary trigger is still
// re-syncing so crankSynchronizationCounter is low - the cam check must stay
// suppressed until the crank has fully re-synced, then engage again.
TEST(SensorCheckerCam, NoSpuriousNoSignalOnRestartUntilResynced) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->camInputs[0] = Gpio::A0;
	engineConfiguration->cranking.rpm = 550;

	setupSensorCheckerPreconditions();
	engine->triggerCentral.vvtState[0][0].hasSignal = true; // edges, but stale position

	// Simulate restart: stop, then begin cranking with the counter freshly reset/low
	engine->rpmCalculator.setRpmValue(0);
	engine->rpmCalculator.setRpmValue(300);
	ASSERT_TRUE(engine->rpmCalculator.isCranking());
	engine->triggerCentral.triggerState.crankSynchronizationCounter = 5;

	clearWarnings();
	engine->module<SensorChecker>()->onSlowCallback();
	// Not yet re-synced -> no spurious code
	EXPECT_FALSE(hasError(ObdCode::OBD_Camshaft_Position_Sensor_B1I_NoSignal));

	// Once the crank has fully re-synced the check engages again (positive control)
	engine->triggerCentral.triggerState.crankSynchronizationCounter = 25;
	clearWarnings();
	engine->module<SensorChecker>()->onSlowCallback();
	EXPECT_TRUE(hasError(ObdCode::OBD_Camshaft_Position_Sensor_B1I_NoSignal));
}

// ==================== Engine-stop resets trigger state ====================
// The stale-counter-on-restart bug: when the engine stops, crankSynchronizationCounter
// must be cleared so a restart starts from a clean slate (otherwise a stale count keeps
// the cam checks enabled before the cam has re-synced).

// When no trigger events have arrived recently, periodicSlowCallback() should detect the
// stop and reset the crank sync counter (via OnTriggerSynchronizationLost).
TEST(TriggerStateOnStop, EngineStopResetsCrankSyncCounter) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	// No trigger events fired -> engine has not moved recently
	advanceTimeUs(10e6);
	engine->rpmCalculator.setRpmValue(2000);
	auto& triggerState = engine->triggerCentral.triggerState;
	triggerState.crankSynchronizationCounter = 25;

	ASSERT_FALSE(engine->triggerCentral.engineMovedRecently(getTimeNowNt()));
	ASSERT_FALSE(engine->rpmCalculator.isStopped());

	engine->periodicSlowCallback();

	EXPECT_EQ(0, triggerState.crankSynchronizationCounter);
	EXPECT_TRUE(engine->rpmCalculator.isStopped());
}

// Negative control: while the engine is genuinely moving, periodicSlowCallback() must
// NOT reset the counter.
TEST(TriggerStateOnStop, RunningEngineKeepsCrankSyncCounter) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);

	// Fire trigger events so the engine has moved recently
	eth.fireTriggerEvents2(/*count*/ 36, /*delayMs*/ 5);
	engine->rpmCalculator.setRpmValue(2000);
	auto& triggerState = engine->triggerCentral.triggerState;
	triggerState.crankSynchronizationCounter = 25;

	ASSERT_TRUE(engine->triggerCentral.engineMovedRecently(getTimeNowNt()));
	ASSERT_FALSE(engine->rpmCalculator.isStopped());

	engine->periodicSlowCallback();

	EXPECT_EQ(25, triggerState.crankSynchronizationCounter);
	EXPECT_FALSE(engine->rpmCalculator.isStopped());
}
