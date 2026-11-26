package com.rusefi.autotest;

import com.rusefi.FileLog;
import com.rusefi.TestingUtils;
import com.rusefi.Timeouts;
import com.rusefi.autodetect.PortDetector;
import com.rusefi.binaryprotocol.BinaryProtocol;
import com.rusefi.config.generated.Fields;
import com.rusefi.io.ConnectionStateListener;
import com.rusefi.io.LinkManager;
import org.jetbrains.annotations.NotNull;

import java.io.File;
import java.util.Arrays;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

import static com.rusefi.IoUtil.sleepSeconds;

/**
 * this class helps us to connect to real ECU once per JVM invocation
 */
public class ControllerConnectorState {
    public volatile static String firmwareVersion;
    private static LinkManager linkManager;
    private static String port;

    /**
     * The ECU reboots this long after it confirms the reboot command (see scheduleReboot() in the firmware)
     */
    private static final int REBOOT_DELAY_MS = 3 * Timeouts.SECOND;
    /**
     * Includes the USB re-enumeration, which can be slow when the ECU is forwarded into a VM/WSL
     */
    private static final int PORT_RETURN_TIMEOUT_MS = 30 * Timeouts.SECOND;
    private static final int CONNECT_TIMEOUT_SEC = 60;

    @NotNull
    public synchronized static LinkManager getLinkManager() {
        if (linkManager != null)
            return linkManager;

        LinkManager linkManager = new LinkManager();
        linkManager.getEngineState().registerStringValueAction(Fields.PROTOCOL_VERSION_TAG, (String firmwareVersion1) -> firmwareVersion = firmwareVersion1);

        /**
         * with real hardware we have noise on all analog inputs which gives us random sensor data, we cannot really
         * test exact numbers yet
         */
        TestingUtils.isRealHardware = true;
        FileLog.MAIN.start();

        String port = System.getenv("HARDWARE_CI_SERIAL_DEVICE");
        if (port == null) {
            port = System.getProperty("ecu.port");
            if (port == null) {
                port = PortDetector.autoDetectSerial().getSerialPort();
                if (port == null)
                    throw new IllegalStateException("ECU serial not detected");
                System.out.println("Auto-connected to " + port);
            }
        }

        linkManager.getEngineState().registerStringValueAction(Fields.PROTOCOL_OUTPIN, (s) -> { });
        connect(linkManager, port);
        ControllerConnectorState.port = port;
        ControllerConnectorState.linkManager = linkManager;
        return linkManager;
    }

    /**
     * After the ECU was told to reboot: drop the connection, wait for the ECU to go away and come back, then
     * reconnect the same {@link LinkManager} (so the command queue and everything holding it stay valid) and
     * re-read the tune.
     */
    public synchronized static void reconnectAfterReboot() {
        if (linkManager == null)
            throw new IllegalStateException("Not connected");

        linkManager.close();

        if (isDevicePath(port)) {
            // Seeing the port go proves the reboot happened, rather than reconnecting to an ECU that ignored it
            waitForPort(false, REBOOT_DELAY_MS + 10 * Timeouts.SECOND);
            waitForPort(true, PORT_RETURN_TIMEOUT_MS);
        } else {
            sleepSeconds(REBOOT_DELAY_MS / Timeouts.SECOND + 5);
        }
        // let the USB device settle before opening it
        sleepSeconds(1);

        connect(linkManager, port);
    }

    private static void connect(LinkManager linkManager, String port) {
        CountDownLatch connected = new CountDownLatch(1);
        AtomicReference<String> failure = new AtomicReference<>();
        linkManager.startAndConnect(port, new ConnectionStateListener() {
            @Override
            public void onConnectionFailed(String s) {
                failure.set(s);
                connected.countDown();
            }

            @Override
            public void onConnectionEstablished() {
                connected.countDown();
            }
        });

        try {
            if (!connected.await(CONNECT_TIMEOUT_SEC, TimeUnit.SECONDS))
                throw new IllegalStateException("Not connected to " + port + " in " + CONNECT_TIMEOUT_SEC + "s");
        } catch (InterruptedException e) {
            throw new IllegalStateException(e);
        }
        if (failure.get() != null)
            throw new IllegalStateException("Connection to " + port + " failed: " + failure.get());
    }

    /**
     * A /dev path we can watch disappear and reappear (Linux/macOS), unlike a Windows COM port name or TCP
     */
    private static boolean isDevicePath(String port) {
        return port.startsWith("/dev/");
    }

    private static void waitForPort(boolean present, int timeoutMs) {
        long start = System.currentTimeMillis();
        while (new File(port).exists() != present) {
            if (System.currentTimeMillis() - start > timeoutMs) {
                throw new IllegalStateException(port + (present ? " did not come back" : " did not go away")
                        + " within " + timeoutMs + "ms. Visible: " + Arrays.toString(new File("/dev/serial/by-id").list()));
            }
            BinaryProtocol.sleep(200);
        }
    }
}
