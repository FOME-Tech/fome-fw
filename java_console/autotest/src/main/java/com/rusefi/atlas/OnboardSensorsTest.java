package com.rusefi.atlas;

import com.rusefi.RusefiTestBase;
import com.rusefi.config.generated.Fields;
import com.rusefi.core.Sensor;
import com.rusefi.enums.engine_type_e;
import org.junit.Before;
import org.junit.Test;

import static com.rusefi.functional_tests.EcuTestHelper.assertSensorSettles;
import static org.junit.Assert.assertEquals;

/**
 * Sensors built in to Atlas, which need no configuration and no rig wiring. Each reads 0 when it isn't registered
 * or its reading has gone stale, so a believable value means it was set up at boot and is still being read.
 */
public class OnboardSensorsTest extends RusefiTestBase {
    /**
     * LPS25/LPS22 on bit-banged I2C, used as baro when no analog baro is configured.
     * Indoor air from sea level up to ~2000 m, with weather on top: 75 to 108 kPa
     */
    private static final double BARO_KPA = 91.5;
    private static final double BARO_TOLERANCE_KPA = 16.5;

    private static final double SENSOR_SUPPLY_VOLTS = 5;
    private static final double SENSOR_SUPPLY_TOLERANCE_VOLTS = 0.2;

    private static final double VBOOST_VOLTS = 8;
    private static final double VBOOST_TOLERANCE_VOLTS = 0.5;

    @Before
    public void setUpOnboardSensors() {
        ecu.setEngineType(engine_type_e.HW_CI_RIG);
        ecu.stopEngine();
        ecu.readConfigurationFromEcu();
        // An analog baro would take precedence over the onboard one
        assertEquals("analog baro channel", 0, (int) ecu.getField(Fields.BAROSENSOR_HWCHANNEL));
    }

    @Test
    public void testOnboardSensors() {
        assertOnboardSensors("");
    }

    @Test
    public void testOnboardSensorsSurviveStoppedBurn() {
        ecu.burnUnrelatedChange();
        assertOnboardSensors(" after a stopped burn");
    }

    private static void assertOnboardSensors(String when) {
        assertSensorSettles("Onboard baro" + when, Sensor.BARO, BARO_KPA, BARO_TOLERANCE_KPA);
        assertSensorSettles("Sensor supply" + when, Sensor.SENSOR_SUPPLY, SENSOR_SUPPLY_VOLTS, SENSOR_SUPPLY_TOLERANCE_VOLTS);
        assertSensorSettles("Vboost" + when, Sensor.VBOOST, VBOOST_VOLTS, VBOOST_TOLERANCE_VOLTS);
    }
}
