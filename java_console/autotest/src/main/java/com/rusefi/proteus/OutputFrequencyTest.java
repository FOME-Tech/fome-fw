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

/**
 * PWM outputs start, follow their frequency, move and stop on a burn, with no reboot (hw_ci_plan.md T5). Each output
 * goes on Ign 9, read back as a frequency by aux speed 1 on Digital 3 (loop A); aux speed 2 on Digital 4 reads Ign 8
 * (loop B). Aux speed reads its input frequency in Hz.
 */
public class OutputFrequencyTest extends RusefiTestBase {
    private static final double HZ_TOLERANCE = 3;
    private static final int RUNNING_RPM = 1000;

    @Before
    public void setUpLoops() {
        ecu.setEngineType(engine_type_e.HW_CI_RIG);
        // setEngineType leaves the stimulator running
        ecu.stopEngine();
        ecu.readConfigurationFromEcu();

        ecu.writeConfiguration(image -> {
            Fields.AUXSPEEDSENSORINPUTPIN1.setValue(image, Fields.AUXSPEEDSENSORINPUTPIN1.enumIndex(Rig.FREQUENCY_A_IN));
            Fields.AUXSPEEDSENSORINPUTPIN2.setValue(image, Fields.AUXSPEEDSENSORINPUTPIN2.enumIndex(Rig.FREQUENCY_B_IN));
        });
        ecu.burn();

        // Nothing drives either loop yet
        assertLoopA("before any output is assigned", 0);
        assertLoopB("before any output is assigned", 0);
    }

    @After
    public void stopEngineAfter() {
        if (ecu != null)
            ecu.stopEngine();
    }

    /**
     * Audit 3.7, 2.3: GPPWM starts, follows its frequency while running, and moves pins, all by burn
     */
    @Test
    public void testGppwm() {
        ecu.writeConfiguration(image -> {
            FrequencyLoops.writeSources(image, 50);
            // Only channel 1 for this test
            Fields.GPPWM2_PIN.setValue(image, Fields.GPPWM2_PIN.enumIndex(Rig.NONE));
        });
        ecu.burn();
        assertLoopA("GPPWM assigned", FrequencyLoops.LOOP_A_HZ);
        assertLoopB("GPPWM assigned to loop A only", 0);

        ecu.startEngine(RUNNING_RPM);
        ecu.setFieldAndBurn(Fields.GPPWM1_PWMFREQUENCY, 140);
        assertLoopA("GPPWM frequency changed while running", 140);

        ecu.setField(Fields.GPPWM1_PIN, Rig.FREQUENCY_B_OUT);
        ecu.burn();
        assertLoopA("GPPWM moved off loop A", 0);
        assertLoopB("GPPWM moved to loop B", 140);

        ecu.setField(Fields.GPPWM1_PIN, Rig.NONE);
        ecu.burn();
        assertLoopB("GPPWM unassigned", 0);
    }

    /**
     * Audit 3.1: tach from pin NONE to assigned, by burn. Frequency = pulses per rev * rpm / 60
     */
    @Test
    public void testTach() {
        ecu.startEngine(RUNNING_RPM);

        ecu.writeConfiguration(image -> {
            Fields.TACHOUTPUTPIN.setValue(image, Fields.TACHOUTPUTPIN.enumIndex(Rig.FREQUENCY_A_OUT));
            // autoscale 0.001: 6 pulses per rev, 100 Hz at 1000 rpm
            Fields.TACHPULSEPERREV.setValue(image, 6000);
            Fields.TACHPULSEDURATIONASDUTYCYCLE.setValue(image, 1);
            Fields.TACHPULSEDURACTIONMS.setValue(image, 0.5);
        });
        ecu.burn();
        assertLoopA("tach assigned", 6 * RUNNING_RPM / 60);

        ecu.setField(Fields.TACHOUTPUTPIN, Rig.NONE);
        ecu.burn();
        assertLoopA("tach unassigned", 0);
    }

    /**
     * Audit 3.2: speedometer from pin NONE to assigned, by burn. VSS reads GPPWM 2 on loop B, so aux speed 2 makes
     * way for it there. Frequency = kph * pulses per km / 3600
     */
    @Test
    public void testSpeedometer() {
        ecu.writeConfiguration(image -> {
            FrequencyLoops.writeSources(image, 50);
            Fields.GPPWM1_PIN.setValue(image, Fields.GPPWM1_PIN.enumIndex(Rig.NONE));
            Fields.AUXSPEEDSENSORINPUTPIN2.setValue(image, Fields.AUXSPEEDSENSORINPUTPIN2.enumIndex(Rig.NONE));

            // 0.9 kph per Hz: 135 kph from loop B's 150 Hz
            Fields.DRIVEWHEELREVPERKM.setValue(image, 1000);
            Fields.VSSGEARRATIO.setValue(image, 1000);
            Fields.VSSTOOTHCOUNT.setValue(image, 4);
            Fields.VEHICLESPEEDSENSORINPUTPIN.setValue(image, Fields.VEHICLESPEEDSENSORINPUTPIN.enumIndex(Rig.FREQUENCY_B_IN));
        });
        ecu.burn();
        assertSensorSettles("VSS from loop B", Sensor.vehicleSpeedKph, 135, 2);

        ecu.writeConfiguration(image -> {
            Fields.SPEEDOMETEROUTPUTPIN.setValue(image, Fields.SPEEDOMETEROUTPUTPIN.enumIndex(Rig.FREQUENCY_A_OUT));
            // 135 kph * 2400 / 3600 = 90 Hz
            Fields.SPEEDOMETERPULSEPERKM.setValue(image, 2400);
        });
        ecu.burn();
        assertLoopA("speedometer assigned", 90);

        ecu.setField(Fields.SPEEDOMETEROUTPUTPIN, Rig.NONE);
        ecu.burn();
        assertLoopA("speedometer unassigned", 0);
    }

    /**
     * Audit 3.8: alternator output from pin NONE, frequency followed, by burn. Battery voltage on the rig is under
     * the target, so the PID sits at its 90% maximum. Only enabled above cranking RPM.
     */
    @Test
    public void testAlternator() {
        ecu.startEngine(RUNNING_RPM);

        ecu.writeConfiguration(image -> {
            Fields.ALTERNATORCONTROLPIN.setValue(image, Fields.ALTERNATORCONTROLPIN.enumIndex(Rig.FREQUENCY_A_OUT));
            Fields.ALTERNATORPWMFREQUENCY.setValue(image, 120);
            Fields.ISALTERNATORCONTROLENABLED.setValue(image, 1);
        });
        ecu.burn();
        assertLoopA("alternator assigned", 120);

        ecu.setFieldAndBurn(Fields.ALTERNATORPWMFREQUENCY, 80);
        assertLoopA("alternator frequency changed", 80);

        ecu.setField(Fields.ALTERNATORCONTROLPIN, Rig.NONE);
        ecu.burn();
        assertLoopA("alternator unassigned", 0);
    }

    /**
     * Audit 3.9: fan PWM mode switched on and off, frequency followed, by burn. The rig has no CLT, so the fan runs at
     * its safety duty.
     */
    @Test
    public void testFanPwm() {
        ecu.writeConfiguration(image -> {
            Fields.FANPIN.setValue(image, Fields.FANPIN.enumIndex(Rig.FREQUENCY_A_OUT));
            Fields.FAN1USEPWMMODE.setValue(image, 1);
            Fields.FANPWMFREQUENCY.setValue(image, 110);
            Fields.FANPWMSAFETYDUTY.setValue(image, 50);
            Fields.DISABLEFAN1WHENSTOPPED.setValue(image, 0);
        });
        ecu.burn();
        assertLoopA("fan in PWM mode", 110);

        ecu.setFieldAndBurn(Fields.FANPWMFREQUENCY, 60);
        assertLoopA("fan frequency changed", 60);

        // On/off mode: a steady level, no edges
        ecu.setFieldAndBurn(Fields.FAN1USEPWMMODE, 0);
        assertLoopA("fan back to on/off", 0);

        ecu.setField(Fields.FANPIN, Rig.NONE);
        ecu.burn();
        assertLoopA("fan unassigned", 0);
    }

    /**
     * Audit 2.2: boost enabled after boot, and disabled again, by burn. Below its minimum RPM it outputs the safe
     * duty.
     */
    @Test
    public void testBoost() {
        ecu.writeConfiguration(image -> {
            Fields.BOOSTCONTROLPIN.setValue(image, Fields.BOOSTCONTROLPIN.enumIndex(Rig.FREQUENCY_A_OUT));
            Fields.BOOSTPWMFREQUENCY.setValue(image, 90);
            Fields.BOOSTCONTROLSAFEDUTYCYCLE.setValue(image, 50);
            Fields.ISBOOSTCONTROLENABLED.setValue(image, 1);
        });
        ecu.burn();
        assertLoopA("boost enabled", 90);

        ecu.setFieldAndBurn(Fields.ISBOOSTCONTROLENABLED, 0);
        assertLoopA("boost disabled", 0);
    }

    /**
     * Audit 2.7: the idle solenoid moves pins and follows its frequency, switching to a stepper stops it, and
     * switching back starts it. HW CI firmware drives the idle valve at its manual position, while the engine runs.
     */
    @Test
    public void testIdleSolenoid() {
        ecu.startEngine(RUNNING_RPM);

        ecu.writeConfiguration(image -> {
            Fields.IDLE_SOLENOIDPIN.setValue(image, Fields.IDLE_SOLENOIDPIN.enumIndex(Rig.FREQUENCY_A_OUT));
            Fields.IDLE_SOLENOIDFREQUENCY.setValue(image, 130);
            Fields.MANIDLEPOSITION.setValue(image, 50);
        });
        ecu.burn();
        assertLoopA("idle solenoid moved to loop A", 130);

        ecu.setFieldAndBurn(Fields.IDLE_SOLENOIDFREQUENCY, 70);
        assertLoopA("idle solenoid frequency changed", 70);

        ecu.setFieldAndBurn(Fields.USESTEPPERIDLE, 1);
        assertLoopA("idle switched to a stepper", 0);

        ecu.setFieldAndBurn(Fields.USESTEPPERIDLE, 0);
        assertLoopA("idle switched back to the solenoid", 70);
    }

    private static void assertLoopA(String what, double hz) {
        assertSensorSettles("Loop A, " + what, Sensor.auxSpeed1, hz, HZ_TOLERANCE);
    }

    private static void assertLoopB(String what, double hz) {
        assertSensorSettles("Loop B, " + what, Sensor.auxSpeed2, hz, HZ_TOLERANCE);
    }
}
