package com.rusefi.f4discovery;


import com.rusefi.RusefiTestBase;
import com.rusefi.core.Sensor;
import com.rusefi.core.SensorCentral;
import com.rusefi.enums.SensorType;
import com.rusefi.enums.engine_type_e;
import com.rusefi.functional_tests.EcuTestHelper;
import com.rusefi.waves.EngineChart;
import org.junit.Test;

import java.util.Arrays;

import static com.rusefi.IoUtil.getEnableCommand;
import static org.junit.Assert.*;

/**
 * rusEFI firmware simulator functional test suite
 * <p/>
 * java -cp fome_console.jar com.rusefi.AutoTest
 *
 * @author Andrey Belomutskiy
 * 3/5/14
 */
public class CommonFunctionalTest extends RusefiTestBase {
    /**
     * This method waits for longer then usual.
     * todo: inline this method? complex and less complex seem to have same timeout these days?
     */
    private void sendComplexCommand(String command) {
        ecu.sendCommand(command);
    }

    private static void assertWaveNull(EngineChart chart, String key) {
        assertWaveNull("", chart, key);
    }

    private static void assertWaveNull(EngineChart chart, String... keys) {
        Arrays.stream(keys).peek(k -> assertWaveNull(chart, k));
    }

    private static void assertWaveNull(String msg, EngineChart chart, String key) {
        assertNull(msg + "chart for " + key, chart.get(key));
    }

    private static void assertWaveNotNull(EngineChart chart, String key) {
        assertWaveNotNull("", chart, key);
    }

    private static void assertWaveNotNull(String msg, EngineChart chart, String key) {
        assertNotNull(msg, chart.get(key));
    }

    private static void assertWaveNotNull(EngineChart chart, String... keys) {
        Arrays.stream(keys).peek(k -> assertWaveNotNull(chart, k));
    }

    private static void assertWaveNoRises(EngineChart chart, String key) {
        StringBuilder events = chart.get(key);

        // if no events of this type at all, return since this passes the test
        if (events == null) {
            return;
        }

        // Assert that there are no up (rise) events in the channel's sequence
        assertFalse(events.toString().contains("u"));
    }

    private static void assertWaveNoRises(EngineChart chart, String... keys) {
        Arrays.stream(keys).peek(k -> assertWaveNoRises(chart, k));
    }

}
