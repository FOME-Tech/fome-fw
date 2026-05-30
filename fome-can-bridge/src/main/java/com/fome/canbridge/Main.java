package com.fome.canbridge;

import com.rusefi.io.serial.AbstractIoStream;
import com.rusefi.io.tcp.BinaryProtocolProxy;
import com.rusefi.io.tcp.TcpConnector;
import com.rusefi.io.can.SocketCANIoStream;
import com.rusefi.io.can.SlCanIoStream;
import com.rusefi.ui.StatusConsumer;

import java.io.IOException;
import java.util.concurrent.*;

import com.fome.canbridge.view.BridgeGui;
import com.formdev.flatlaf.FlatDarkLaf;
import javax.swing.*;

public class Main {
    static {
        // Workaround for Windows "Access Denied" when extracting to default %TEMP%
        String tempDir = System.getProperty("user.home") + "/.jSerialComm";
        System.setProperty("java.io.tmpdir", tempDir);
        java.io.File dir = new java.io.File(tempDir);
        if (!dir.exists()) {
            dir.mkdirs();
        }
    }

    public static void main(String[] args) {
        FlatDarkLaf.setup();
        if (args.length > 0) {
            runCli(args);
        } else {
            SwingUtilities.invokeLater(() -> {
                BridgeGui gui = new BridgeGui();
                gui.setOnConnect(config -> connect(config, gui));
                gui.setVisible(true);
            });
        }
    }

    private static void runCli(String[] args) {
        System.out.println("FOME CAN Bridge starting (CLI mode)...");
    }

    private static void connect(BridgeGui.BridgeConfig config, BridgeGui gui) {
        int retries = 3;
        int timeoutSeconds = 10;
        ExecutorService executor = Executors.newSingleThreadExecutor();

        AbstractIoStream canStream = null;

        try {
            for (int i = 1; i <= retries; i++) {
                final int attempt = i;
                gui.logLine("Connection attempt " + i + " of " + retries + " (Timeout: " + timeoutSeconds + "s)...");

                Future<AbstractIoStream> future = executor.submit(() -> {
                    if ("SLCAN".equalsIgnoreCase(config.type)) {
                        return new SlCanIoStream(config.device, config.rxId, config.txId);
                    } else {
                        return new SocketCANIoStream(config.device, config.rxId, config.txId);
                    }
                });

                try {
                    canStream = future.get(timeoutSeconds, TimeUnit.SECONDS);
                    if (canStream != null) break;
                } catch (TimeoutException e) {
                    gui.logLine("Attempt " + attempt + " timed out after " + timeoutSeconds + "s.");
                    future.cancel(true);
                } catch (ExecutionException e) {
                    gui.logLine("Error: " + e.getCause().getMessage());
                } catch (InterruptedException e) {
                    gui.logLine("Connection interrupted.");
                    Thread.currentThread().interrupt();
                    break;
                }

                if (i < retries) {
                    try { Thread.sleep(2000); } catch (InterruptedException ignored) {}
                }
            }
        } finally {
            executor.shutdownNow();
        }

        if (canStream == null) {
            gui.logLine("CRITICAL: Failed to connect after " + retries + " attempts.");
            gui.setConnectEnabled(true);
            return;
        }

        gui.logLine("Connected successfully to CAN.");
        try {
            startProxy(canStream, gui);
            gui.logLine("TCP Proxy started on port " + TcpConnector.DEFAULT_PORT);
        } catch (IOException e) {
            gui.logLine("Failed to start proxy: " + e.getMessage());
            gui.setConnectEnabled(true);
        }
    }

    private static void startProxy(AbstractIoStream tsStream, StatusConsumer status) throws IOException {
        BinaryProtocolProxy.createProxy(tsStream, TcpConnector.DEFAULT_PORT, clientRequest -> {
        }, status);
    }
}
