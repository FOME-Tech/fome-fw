package com.rusefi.proteus;

import com.rusefi.Rig;
import com.rusefi.RusefiTestBase;
import com.rusefi.config.generated.Fields;
import com.rusefi.core.Sensor;
import com.rusefi.core.SensorCentral;
import com.rusefi.enums.engine_type_e;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;

import static com.rusefi.IoUtil.getDisableCommand;
import static com.rusefi.IoUtil.getEnableCommand;
import static com.rusefi.IoUtil.sleepSeconds;
import static com.rusefi.config.generated.Fields.CMD_EXTERNAL_STIMULATION;
import static com.rusefi.config.generated.Fields.CMD_SELF_STIMULATION;
import static com.rusefi.functional_tests.EcuTestHelper.assertSensorSettles;
import static org.junit.Assert.assertEquals;

/**
 * The real trigger input path (hw_ci_plan.md T7): the stimulator drives Ign 11 (crank, channel 1) and Ign 12 (channel 2)
 * in external mode, jumpered into Digital 1 and Digital 2, and the ECU decodes them like a real engine. Burns while
 * running must not cost a trigger error. Port of the Discovery's scheduleBurnDoesNotAffectTriggerIssue2839.
 */
public class HardwareTriggerTest extends RusefiTestBase {
    private static final int RPM = 1200;
    private static final int BURNS = 10;

    @Override
    protected boolean needsHardwareTriggerInput() {
        return true;
    }

    @Before
    public void setUpRig() {
        ecu.setEngineType(engine_type_e.HW_CI_RIG);
        // setEngineType leaves the stimulator running
        ecu.stopEngine();
        ecu.readConfigurationFromEcu();
    }

    @After
    public void stopEngineAfter() {
        if (ecu != null)
            ecu.stopEngine();
    }

    @Test
    public void test60_2BurnsWhileRunning() {
        ecu.setField(Fields.TRIGGER_TYPE, "60/2");
        ecu.burn();

        startHardwareTrigger();
        assertBurnsDontCostTriggerErrors();
    }

    /**
     * The decoded RPM follows the stimulator: it really is coming from the hardware input
     */
    @Test
    public void testRpmFollowsHardwareTrigger() {
        ecu.setField(Fields.TRIGGER_TYPE, "60/2");
        ecu.burn();

        startHardwareTrigger();
        for (int rpm : new int[]{3000, 800, RPM}) {
            ecu.changeRpm(rpm);
            assertSensorSettles("RPM from the hardware trigger", Sensor.RPMValue, rpm, rpm * 0.05);
        }
    }

    /**
     * Both trigger inputs: Miata NA uses both channels, the second on Ign 12 -> Digital 2
     */
    @Test
    public void testTwoChannelTriggerBurnsWhileRunning() {
        ecu.writeConfiguration(image -> {
            Fields.TRIGGER_TYPE.setValue(image, Fields.TRIGGER_TYPE.enumIndex("Miata NA"));
            Fields.TRIGGERINPUTPINS2.setValue(image, Fields.TRIGGERINPUTPINS2.enumIndex(Rig.CAM_IN));
        });
        ecu.burn();

        startHardwareTrigger();
        assertBurnsDontCostTriggerErrors();
    }

    private void startHardwareTrigger() {
        // External mode: the stimulator only drives its pins, the ECU sees edges through the trigger inputs
        ecu.sendCommand(getDisableCommand(CMD_SELF_STIMULATION));
        ecu.sendCommand(getEnableCommand(CMD_EXTERNAL_STIMULATION));
        ecu.changeRpm(RPM);
        // Let it sync and settle before counting errors
        sleepSeconds(3);
        assertSensorSettles("RPM from the hardware trigger", Sensor.RPMValue, RPM, RPM * 0.05);
    }

    private void assertBurnsDontCostTriggerErrors() {
        int errorsBefore = triggerErrors();
        for (int i = 0; i < BURNS; i++) {
            ecu.setUnrelatedChange();
            // F4 defers the flash write while the hardware trigger runs; applying the burn is what's under test
            ecu.burnWithoutWaitingForFlash();
            sleepSeconds(2);
        }
        assertEquals("trigger errors after " + BURNS + " burns while running", errorsBefore, triggerErrors());
        assertSensorSettles("RPM after the burns", Sensor.RPMValue, RPM, RPM * 0.05);
    }

    private static int triggerErrors() {
        return (int) SensorCentral.getInstance().getValue(Sensor.totalTriggerErrorCounter);
    }
}
