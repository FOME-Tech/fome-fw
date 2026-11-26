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
 * Flex fuel sensor faked by GPPWM on the frequency loops (hw_ci_plan.md T6): ethanol % = Hz - 50, fuel temperature
 * from the low time of each pulse (1 ms = -40 C, 5 ms = 125 C). Starts on loop A.
 */
public class FlexLoopTest extends RusefiTestBase {
    /**
     * 30% low time: 3 ms at loop A's 100 Hz
     */
    private static final int DUTY_PERCENT = 70;
    private static final double LOOP_A_FUEL_TEMP = -40 + (3000 - 1000) * 165.0 / 4000;

    private static final double PERCENT_TOLERANCE = 2;
    private static final double TEMP_TOLERANCE = 5;

    private static final int RUNNING_RPM = 1000;

    @Before
    public void setUpFlex() {
        ecu.setEngineType(engine_type_e.HW_CI_RIG);
        // setEngineType leaves the stimulator running
        ecu.stopEngine();
        ecu.readConfigurationFromEcu();

        ecu.writeConfiguration(image -> {
            FrequencyLoops.writeSources(image, DUTY_PERCENT);
            Fields.FLEXSENSORPIN.setValue(image, Fields.FLEXSENSORPIN.enumIndex(Rig.FREQUENCY_A_IN));
        });
        ecu.burn();

        // GPPWM outputs start at boot, so start from a clean boot with everything in place
        ecu.reboot();

        assertFlex("after boot", LOOP_A_HZ);
        assertSensorSettles("Fuel temperature after boot", Sensor.fuelTemp, LOOP_A_FUEL_TEMP, TEMP_TOLERANCE);
    }

    @After
    public void stopEngineAfter() {
        if (ecu != null)
            ecu.stopEngine();
    }

    @Test
    public void testFlexSurvivesStoppedBurn() {
        ecu.burnUnrelatedChange();
        assertFlex("after a stopped burn", LOOP_A_HZ);
    }

    /**
     * Starting the stimulator is itself a stopped configuration change, then burn while running
     */
    @Test
    public void testFlexSurvivesEngineStartAndRunningBurn() {
        ecu.startEngine(RUNNING_RPM);
        assertFlex("with the engine running", LOOP_A_HZ);

        ecu.burnUnrelatedChange();
        assertFlex("after a running burn", LOOP_A_HZ);
    }

    @Test
    public void testMoveFlexStopped() {
        ecu.setField(Fields.FLEXSENSORPIN, Rig.FREQUENCY_B_IN);
        ecu.burn();
        assertFlex("after moving it to loop B", LOOP_B_HZ);
    }

    /**
     * An unassigned flex sensor goes away at once, rather than holding its last reading for its 30 s timeout
     */
    @Test
    public void testUnassignFlex() {
        ecu.setField(Fields.FLEXSENSORPIN, Rig.NONE);
        ecu.burn();
        assertSensorSettles("Flex after unassigning it", Sensor.flexPercent, 0, PERCENT_TOLERANCE);
    }

    private static void assertFlex(String when, int loopHz) {
        assertSensorSettles("Flex " + when, Sensor.flexPercent, loopHz - 50, PERCENT_TOLERANCE);
    }
}
