package com.rusefi;

import com.rusefi.core.MessagesCentral;
import com.rusefi.functional_tests.EcuTestHelper;
import com.rusefi.waves.EngineChart;
import org.junit.*;

import java.util.ArrayList;
import java.util.List;

public class RusefiTestBase {
    /**
     * HW CI firmware reports a main loop stall that overlapped a flash write on a chip that writes flash
     * while running, and so must never stall for it (H7; see main_loop.cpp).
     */
    private static final String UNEXPECTED_FLASH_STALL = "HW CI: unexpected main loop stall";
    private static final List<String> unexpectedFlashStalls = new ArrayList<>();

    static {
        // For the whole run: a report that lands after a test ends fails the next one
        MessagesCentral.getInstance().addListener((clazz, message) -> {
            if (message.contains(UNEXPECTED_FLASH_STALL)) {
                synchronized (unexpectedFlashStalls) {
                    unexpectedFlashStalls.add(message);
                }
            }
        });
    }

    protected EcuTestHelper ecu;

    protected boolean needsHardwareTriggerInput() {
        // Most tests do not, but some may need it
        return false;
    }

    @Before
    public void startUp() {
        ecu = EcuTestHelper.createInstance(needsHardwareTriggerInput());
    }

    @After
    public void checkStackUsage() {
        if (ecu != null)
            ecu.sendCommand("threadsinfo");
    }

    @After
    public void checkNoUnexpectedFlashStall() {
        List<String> stalls;
        synchronized (unexpectedFlashStalls) {
            stalls = new ArrayList<>(unexpectedFlashStalls);
            unexpectedFlashStalls.clear();
        }
        if (!stalls.isEmpty())
            Assert.fail("Flash write stalled the main loop: " + stalls);
    }

    protected EngineChart nextChart() {
        return TestingUtils.nextChart(ecu.commandQueue);
    }
}
