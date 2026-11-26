package com.rusefi.functional_tests;

import com.devexperts.logging.Logging;
import com.opensr5.ConfigurationImage;
import com.rusefi.autotest.ControllerConnectorState;
import com.rusefi.binaryprotocol.BinaryProtocol;
import com.rusefi.config.Field;
import com.rusefi.IoUtil;
import com.rusefi.Timeouts;
import com.rusefi.config.generated.Fields;
import com.rusefi.core.ISensorCentral;
import com.rusefi.core.MessagesCentral;
import com.rusefi.core.Sensor;
import com.rusefi.core.SensorCentral;
import com.rusefi.enums.engine_type_e;
import com.rusefi.io.CommandQueue;
import com.rusefi.io.LinkManager;
import com.rusefi.waves.EngineReport;
import org.jetbrains.annotations.NotNull;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.Consumer;
import java.util.function.Function;

import static com.devexperts.logging.Logging.getLogging;
import static com.rusefi.IoUtil.*;
import static com.rusefi.waves.EngineReport.isCloseEnough;

public class EcuTestHelper {
    public static final Function<String, Object> FAIL = errorCode -> {
        if (errorCode != null)
            throw new IllegalStateException("Failed " + errorCode);
        return null;
    };
    private static final Logging log = getLogging(EcuTestHelper.class);
    private static final int BURN_FLASH_TIMEOUT_SEC = 10;
    private static final int TEXT_TIMEOUT_SEC = 5;
    private static final int COMMUNICATION_TIMEOUT_SEC = 60;

    public static int currentEngineType;
    public final CommandQueue commandQueue;
    @NotNull
    private final LinkManager linkManager;

    /**
     * Applied again after a reboot, since the ECU keeps it in RAM only
     */
    private final boolean allowHardwareTriggerInput;

    private EcuTestHelper(LinkManager linkManager, boolean allowHardwareTriggerInput) {
        this.commandQueue = linkManager.getCommandQueue();
        this.linkManager = linkManager;
        this.allowHardwareTriggerInput = allowHardwareTriggerInput;
    }

    public static void assertRpmDoesNotJump(int rpm, int settleTime, int testDuration, Function<String, Object> callback, CommandQueue commandQueue) {
        IoUtil.changeRpm(commandQueue, rpm);
        sleepSeconds(settleTime);
        AtomicReference<String> result = new AtomicReference<>();
        long start = System.currentTimeMillis();

        ISensorCentral.ListenerToken listener = SensorCentral.getInstance().addListener(Sensor.RPMValue, actualRpm -> {
            if (!isCloseEnough(rpm, actualRpm)) {
                long seconds = (System.currentTimeMillis() - start) / 1000;
                result.set("Got " + actualRpm + " while trying to stay at " + rpm + " after " + seconds + " seconds");
            }
        });

        sleepSeconds(testDuration);
        callback.apply(result.get());

        listener.remove();
    }

    @NotNull
    public LinkManager getLinkManager() {
        return linkManager;
    }

    public static void assertSomewhatClose(double expected, double actual) {
        EcuTestHelper.assertSomewhatClose("", expected, actual);
    }

    public static void assertSomewhatClose(String msg, double expected, double actual) {
        EcuTestHelper.assertSomewhatClose(msg, expected, actual, EngineReport.RATIO);
    }

    /**
     * todo: it's time to leverage junit a bit more properly
     */
    public static void assertSomewhatClose(String msg, double expected, double actual, double ratio) {
        if (!isCloseEnough(expected, actual, ratio))
            throw new IllegalStateException(msg + " Expected " + expected + " but got " + actual);
    }

    @NotNull
    public static EcuTestHelper createInstance() {
        return createInstance(false);
    }

    @NotNull
    public static EcuTestHelper createInstance(boolean allowHardwareTriggerInput) {
        EcuTestHelper ecu = new EcuTestHelper(ControllerConnectorState.getLinkManager(), allowHardwareTriggerInput);
        ecu.applyTestModes();
        return ecu;
    }

    private void applyTestModes() {
        if (allowHardwareTriggerInput) {
            sendCommand(getEnableCommand(Fields.CMD_TRIGGER_HW_INPUT));
        } else {
            sendCommand(getDisableCommand(Fields.CMD_TRIGGER_HW_INPUT));
        }
        enableFunctionalMode();
    }

    /**
     * Our copy of the tune: as read from the ECU at connect, plus everything written since
     */
    @NotNull
    public ConfigurationImage getConfiguration() {
        ConfigurationImage image = linkManager.getCurrentStreamState().getControllerConfiguration();
        if (image == null)
            throw new IllegalStateException("No configuration read from the ECU");
        return image;
    }

    /**
     * Raw (unscaled) value of a field in our copy of the tune
     */
    public double getField(Field field) {
        return field.getValue(getConfiguration(), 1);
    }

    /**
     * Change the tune in the ECU's RAM the way TunerStudio does: edit a copy of the tune, then write the bytes that
     * changed. Takes effect at once, but is lost on reboot unless followed by {@link #burn()}.
     */
    public void writeConfiguration(Consumer<ConfigurationImage> edit) {
        onCommunicationThread(() -> {
            BinaryProtocol bp = linkManager.getCurrentStreamState();
            ConfigurationImage before = getConfiguration();
            ConfigurationImage after = before.clone();
            edit.accept(after);

            byte[] old = before.getContent();
            byte[] content = after.getContent();
            int offset = 0;
            while (offset < content.length) {
                if (old[offset] == content[offset]) {
                    offset++;
                    continue;
                }
                // Write from the first changed byte, up to one chunk at a time
                int end = Math.min(offset + Fields.BLOCKING_FACTOR, content.length);
                while (end > offset && old[end - 1] == content[end - 1])
                    end--;
                bp.writeData(content, offset, offset, end - offset);
                offset = end;
            }
        });
    }

    /**
     * Write a raw (unscaled) value to one field, without burning
     */
    public void setField(Field field, double rawValue) {
        writeConfiguration(image -> field.setValue(image, rawValue));
    }

    /**
     * Write an enum field by the name TunerStudio shows for the value, e.g. a pin by its connector name
     * ({@link com.rusefi.Rig#CRANK_OUT}), without burning
     */
    public void setField(Field field, String enumValueName) {
        setField(field, field.enumIndex(enumValueName));
    }

    /**
     * The TunerStudio name of an enum field's value in our copy of the tune
     */
    public String getFieldName(Field field) {
        int value = (int) getField(field);
        String name = field.enumName(value);
        if (name == null)
            throw new IllegalStateException(field.getName() + " has value " + value + ", outside its list");
        return name;
    }

    /**
     * Write a raw (unscaled) value to one field and burn it
     */
    public void setFieldAndBurn(Field field, double rawValue) {
        setField(field, rawValue);
        burn();
    }

    /**
     * Burn the tune the way TunerStudio does, and wait for the ECU to finish writing it to flash, so that a reboot
     * straight after doesn't lose it. Does nothing if nothing was written since the last burn.
     */
    public void burn() {
        if (!linkManager.getCurrentStreamState().isBurnPending()) {
            log.info("burn: nothing written since the last burn");
            return;
        }

        // The ECU acknowledges the burn at once, then saves the flash and prints the result
        CountDownLatch flashed = new CountDownLatch(1);
        AtomicReference<String> flashResult = new AtomicReference<>();
        AtomicBoolean sawOurBurn = new AtomicBoolean();
        MessagesCentral.MessageListener listener = (clazz, message) -> {
            if (message.contains("TS -> Burn")) {
                sawOurBurn.set(true);
            } else if (sawOurBurn.get() && (message.contains("FLASH_SUCCESS") || message.contains("Flashing failed"))) {
                flashResult.set(message);
                flashed.countDown();
            }
        };
        MessagesCentral.getInstance().addListener(listener);
        try {
            onCommunicationThread(() -> linkManager.getCurrentStreamState().burn());
            if (!flashed.await(BURN_FLASH_TIMEOUT_SEC, TimeUnit.SECONDS)) {
                throw new IllegalStateException("ECU did not report a flash write within " + BURN_FLASH_TIMEOUT_SEC
                        + "s of the burn. On F4 the write waits until the engine stops, unless it's self-stimulated");
            }
        } catch (InterruptedException e) {
            throw new IllegalStateException(e);
        } finally {
            MessagesCentral.getInstance().removeListener(listener);
        }
        if (!flashResult.get().contains("FLASH_SUCCESS"))
            throw new IllegalStateException("ECU failed to burn: " + flashResult.get());
    }

    /**
     * A config value as the firmware sees it, by its C name (e.g. "tractionControl.drivenAxleIsFront"), using the
     * console's 'get' command. That lookup is generated from the C layout, so comparing it with a field written by
     * offset checks the Java and C layouts agree. Covers scalars and bits, not enums.
     */
    public double getFirmwareValue(String cName) {
        String prefix = cName + " value: ";
        CountDownLatch received = new CountDownLatch(1);
        AtomicReference<String> value = new AtomicReference<>();
        MessagesCentral.MessageListener listener = (clazz, message) -> {
            if (message.startsWith(prefix)) {
                value.set(message.substring(prefix.length()).trim());
                received.countDown();
            }
        };
        MessagesCentral.getInstance().addListener(listener);
        try {
            sendCommand("get " + cName);
            if (!received.await(TEXT_TIMEOUT_SEC, TimeUnit.SECONDS))
                throw new IllegalStateException("No value for [" + cName + "] in " + TEXT_TIMEOUT_SEC + "s");
        } catch (InterruptedException e) {
            throw new IllegalStateException(e);
        } finally {
            MessagesCentral.getInstance().removeListener(listener);
        }
        return Double.parseDouble(value.get());
    }

    /**
     * Burn a change nothing on the rig depends on, to see what a burn does to everything else
     */
    public void burnUnrelatedChange() {
        double threshold = getField(Fields.ENGINESNIFFERRPMTHRESHOLD);
        setFieldAndBurn(Fields.ENGINESNIFFERRPMTHRESHOLD, threshold == 2500 ? 2600 : 2500);
    }

    /**
     * Wait up to 5 seconds for a reading to arrive, then require it to hold for a second, so a reading that's only on
     * its way out (a frequency sensor times out 500ms after its last edge) doesn't pass
     */
    public static void assertSensorSettles(String what, Sensor sensor, double expected, double tolerance) {
        long deadline = System.currentTimeMillis() + 5 * Timeouts.SECOND;
        double value = SensorCentral.getInstance().getValue(sensor);
        while (Math.abs(value - expected) > tolerance) {
            if (System.currentTimeMillis() > deadline)
                throw new AssertionError(what + ": expected " + expected + " but read " + value);
            BinaryProtocol.sleep(100);
            value = SensorCentral.getInstance().getValue(sensor);
        }

        long holdUntil = System.currentTimeMillis() + Timeouts.SECOND;
        while (System.currentTimeMillis() < holdUntil) {
            BinaryProtocol.sleep(100);
            value = SensorCentral.getInstance().getValue(sensor);
            if (Math.abs(value - expected) > tolerance)
                throw new AssertionError(what + ": reached " + expected + " but then read " + value);
        }
    }

    /**
     * Read the whole tune back from the ECU into our copy, e.g. to check that a write really landed
     */
    public void readConfigurationFromEcu() {
        onCommunicationThread(() -> {
            BinaryProtocol bp = linkManager.getCurrentStreamState();
            bp.readImage(Fields.TOTAL_CONFIG_SIZE);
            if (bp.isClosed)
                throw new IllegalStateException("Failed to read the tune from the ECU");
        });
    }

    /**
     * Reboot the ECU and reconnect. Afterwards our copy of the tune is what the ECU read back from flash, and the
     * RAM-only test modes from {@link #createInstance} are applied again.
     */
    public void reboot() {
        sendCommand(Fields.CMD_REBOOT);
        ControllerConnectorState.reconnectAfterReboot();
        applyTestModes();
    }

    /**
     * Run on the single thread that talks to the ECU, which is the only one allowed to (see
     * {@link LinkManager#assertCommunicationThread()}), and rethrow anything it throws
     */
    private void onCommunicationThread(Runnable action) {
        try {
            linkManager.submit(action).get(COMMUNICATION_TIMEOUT_SEC, TimeUnit.SECONDS);
        } catch (ExecutionException e) {
            if (e.getCause() instanceof RuntimeException)
                throw (RuntimeException) e.getCause();
            throw new IllegalStateException(e.getCause());
        } catch (InterruptedException | TimeoutException e) {
            throw new IllegalStateException(e);
        }
    }

    public void sendCommand(String command) {
        sendCommand(command, Timeouts.CMD_TIMEOUT);
    }

    public void sendCommand(String command, int timeoutMs) {
        TestHelper.INSTANCE.assertNotFatal();
        IoUtil.sendBlockingCommand(command, timeoutMs, commandQueue);
    }

    /**
     * this seem to adjust engine sniffer behaviour
     */
    public void enableFunctionalMode() {
        sendCommand(getEnableCommand(Fields.CMD_FUNCTIONAL_TEST_MODE));
    }

    public void changeRpm(final int rpm) {
        IoUtil.changeRpm(commandQueue, rpm);
    }

    /**
     * Stop the trigger stimulator and wait until the ECU considers the engine stopped (RPM 0)
     */
    public void stopEngine() {
        sendCommand(getDisableCommand(Fields.CMD_SELF_STIMULATION));

        // RPM also reads 0 before a just-started engine has spun up, so wait out the ECU's stop
        // detection (2 s after the last trigger event) and 'engine moved recently' (up to 2.4 s,
        // which keeps outputs like the idle valve driving) before trusting it
        sleepSeconds(3);

        long start = System.currentTimeMillis();
        while (SensorCentral.getInstance().getValue(Sensor.RPMValue) != 0) {
            if (System.currentTimeMillis() - start > 10 * Timeouts.SECOND)
                throw new IllegalStateException("Engine did not stop, RPM "
                        + SensorCentral.getInstance().getValue(Sensor.RPMValue));
            sleepSeconds(1);
        }
    }

    /**
     * Restart the trigger stimulator at the given RPM
     */
    public void startEngine(final int rpm) {
        sendCommand(getEnableCommand(Fields.CMD_SELF_STIMULATION));
        changeRpm(rpm);
    }

    public void setEngineType(engine_type_e engine_type_e) {
        int type = engine_type_e.ordinal();
        log.info("AUTOTEST setEngineType " + type);
        currentEngineType = type;
//        sendCommand(CMD_PINS);
        /*
         * we need to stop all activity - that means:
         * - stopping input event
         * - waiting for scheduled actuator actions to run out
         * - disabling PWM
         */
        sendCommand(getDisableCommand(Fields.CMD_SELF_STIMULATION));
        sendCommand(getDisableCommand(Fields.CMD_INJECTION));
        sendCommand(getDisableCommand(Fields.CMD_IGNITION));
        sendCommand(getDisableCommand(Fields.CMD_PWM));
        // changing engine type while engine is running does not work well - we rightfully
        // get invalid configuration critical errors
        sleepSeconds(2);
        sendCommand("set " + Fields.CMD_ENGINE_TYPE + " " + type, Timeouts.SET_ENGINE_TIMEOUT);
        // TODO: document the reason for this sleep?!
        sleepSeconds(1);
        sendCommand(getEnableCommand(Fields.CMD_PWM));
        sendCommand(getEnableCommand(Fields.CMD_SELF_STIMULATION));
//        // we need to skip one chart since it might have been produced with previous engine type
//        TestingUtils.nextChart(commandQueue);
    }
}
