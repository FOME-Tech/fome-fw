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
	engine->triggerCentral.m_lastEventTimer.reset();
}

// Validate that when the crank has synced but no cam edges have arrived,
// the sensor checker reports a cam no-signal error.
TEST(SensorCheckerCam, NoSignalWhenNoEdges) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->camInputs[0] = Gpio::A0;

	setupSensorCheckerPreconditions();

	auto& vvtDecoder = engine->triggerCentral.vvtState[0][0];
	// Cam has NOT received any edges
	ASSERT_FALSE(vvtDecoder.hasSignal);

	engine->module<SensorChecker>()->onSlowCallback();

	EXPECT_TRUE(hasError(ObdCode::OBD_Camshaft_Position_Sensor_B1I_NoSignal));
}

// Validate that when cam edges have been received and VVT has synced,
// no cam no-signal error is set.
TEST(SensorCheckerCam, NoErrorWhenSignalPresent) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->camInputs[0] = Gpio::A0;

	setupSensorCheckerPreconditions();

	auto& vvtDecoder = engine->triggerCentral.vvtState[0][0];
	// Simulate cam edges received
	vvtDecoder.hasSignal = true;

	// Simulate a recent valid VVT position so scenario 2 check passes
	engine->triggerCentral.vvtPosition[0][0].t.reset();

	engine->module<SensorChecker>()->onSlowCallback();

	EXPECT_FALSE(hasError(ObdCode::OBD_Camshaft_Position_Sensor_B1I_NoSignal));
}

// Validate that hasSignal survives counter rollover — this is the bug we fixed.
// Previously the check was `edgeCountRise == 0`, which would false-positive on rollover.
TEST(SensorCheckerCam, SignalSurvivesCounterRollover) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->camInputs[0] = Gpio::A0;

	setupSensorCheckerPreconditions();

	auto& vvtDecoder = engine->triggerCentral.vvtState[0][0];
	// Simulate: edges have been received (hasSignal = true), but counter rolled over to 0
	vvtDecoder.hasSignal = true;
	vvtDecoder.edgeCountRise = 0;

	// Simulate a recent valid VVT position so scenario 2 check passes
	engine->triggerCentral.vvtPosition[0][0].t.reset();

	engine->module<SensorChecker>()->onSlowCallback();

	// Should NOT report no-signal, because hasSignal is true despite counter being 0
	EXPECT_FALSE(hasError(ObdCode::OBD_Camshaft_Position_Sensor_B1I_NoSignal));
}
