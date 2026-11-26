package com.rusefi.proteus;

import com.rusefi.Rig;
import com.rusefi.RusefiTestBase;
import com.rusefi.config.generated.Fields;
import com.rusefi.core.Sensor;
import com.rusefi.enums.engine_type_e;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;

import static com.rusefi.functional_tests.EcuTestHelper.assertSensorSettles;
import static com.rusefi.proteus.FrequencyLoops.LOOP_A_HZ;
import static com.rusefi.proteus.FrequencyLoops.LOOP_B_HZ;

/**
 * Frequency inputs keep working across burns (hw_ci_plan.md T6). GPPWM drives both frequency loops: Ign 9 -> Digital 3
 * (loop A) and Ign 8 -> Digital 4 (loop B), at different frequencies so a moved input shows. VSS starts on loop A and
 * aux speed 1 on loop B.
 */
public class FrequencyLoopTest extends RusefiTestBase {
    // VSS kph = Hz * 3600 / (wheel rev/km * gear ratio * teeth) = Hz * 3600 / (1000 * 1 * 4) = 0.9 * Hz
    private static final double KPH_PER_HZ = 0.9;

    private static final double KPH_TOLERANCE = 2;
    private static final double HZ_TOLERANCE = 2;

    private static final int RUNNING_RPM = 1000;

    @Before
    public void setUpLoops() {
        ecu.setEngineType(engine_type_e.HW_CI_RIG);
        // setEngineType leaves the stimulator running
        ecu.stopEngine();
        ecu.readConfigurationFromEcu();

        ecu.writeConfiguration(image -> {
            FrequencyLoops.writeSources(image, 50);

            Fields.DRIVEWHEELREVPERKM.setValue(image, 1000);
            // autoscale 0.001
            Fields.VSSGEARRATIO.setValue(image, 1000);
            Fields.VSSTOOTHCOUNT.setValue(image, 4);

            Fields.VEHICLESPEEDSENSORINPUTPIN.setValue(image, Fields.VEHICLESPEEDSENSORINPUTPIN.enumIndex(Rig.FREQUENCY_A_IN));
            Fields.AUXSPEEDSENSORINPUTPIN1.setValue(image, Fields.AUXSPEEDSENSORINPUTPIN1.enumIndex(Rig.FREQUENCY_B_IN));
        });
        ecu.burn();

        // GPPWM outputs start at boot, so start from a clean boot with everything in place
        ecu.reboot();

        assertVss("after boot", LOOP_A_HZ);
        assertAuxSpeed("after boot", LOOP_B_HZ);
    }

    @After
    public void stopEngineAfter() {
        if (ecu != null)
            ecu.stopEngine();
    }

    /**
     * Audit 1.1, 2.5: a burn with the engine stopped must not lose frequency inputs
     */
    @Test
    public void testInputsSurviveStoppedBurn() {
        ecu.burnUnrelatedChange();

        assertVss("after a stopped burn", LOOP_A_HZ);
        assertAuxSpeed("after a stopped burn", LOOP_B_HZ);
    }

    @Test
    public void testInputsSurviveRunningBurn() {
        startEngine();
        ecu.burnUnrelatedChange();

        assertVss("after a running burn", LOOP_A_HZ);
        assertAuxSpeed("after a running burn", LOOP_B_HZ);
    }

    /**
     * Swap the two inputs' pins in one burn: each has to let go of its pin before the other claims it
     */
    @Test
    public void testSwapPinsStopped() {
        swapPins();

        assertVss("after swapping pins, stopped", LOOP_B_HZ);
        assertAuxSpeed("after swapping pins, stopped", LOOP_A_HZ);
    }

    /**
     * Audit 2.4: sensor changes apply on a running burn too
     */
    @Test
    public void testSwapPinsRunning() {
        startEngine();
        swapPins();

        assertVss("after swapping pins, running", LOOP_B_HZ);
        assertAuxSpeed("after swapping pins, running", LOOP_A_HZ);
    }

    /**
     * Audit 2.5: an unassigned VSS goes invalid (reads 0), rather than a stale sensor hanging on to its last value
     */
    @Test
    public void testUnassignVss() {
        ecu.setField(Fields.VEHICLESPEEDSENSORINPUTPIN, Rig.NONE);
        ecu.burn();

        assertVss("after unassigning VSS", 0);
        assertAuxSpeed("after unassigning VSS", LOOP_B_HZ);
    }

    private void startEngine() {
        ecu.startEngine(RUNNING_RPM);
        assertVss("with the engine running", LOOP_A_HZ);
        assertAuxSpeed("with the engine running", LOOP_B_HZ);
    }

    private void swapPins() {
        ecu.writeConfiguration(image -> {
            Fields.VEHICLESPEEDSENSORINPUTPIN.setValue(image, Fields.VEHICLESPEEDSENSORINPUTPIN.enumIndex(Rig.FREQUENCY_B_IN));
            Fields.AUXSPEEDSENSORINPUTPIN1.setValue(image, Fields.AUXSPEEDSENSORINPUTPIN1.enumIndex(Rig.FREQUENCY_A_IN));
        });
        ecu.burn();
    }

    private void assertVss(String when, int loopHz) {
        assertSensorSettles("VSS " + when, Sensor.vehicleSpeedKph, loopHz * KPH_PER_HZ, KPH_TOLERANCE);
    }

    private void assertAuxSpeed(String when, int loopHz) {
        assertSensorSettles("Aux speed 1 " + when, Sensor.auxSpeed1, loopHz, HZ_TOLERANCE);
    }
}
