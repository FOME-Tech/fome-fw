package com.rusefi;

import com.rusefi.binaryprotocol.BinaryProtocol;
import com.rusefi.binaryprotocol.IncomingDataBuffer;
import com.rusefi.config.generated.Fields;
import com.rusefi.io.IoStream;
import com.rusefi.io.LinkManager;
import com.rusefi.libopenblt.XcpSettings;
import com.rusefi.maintenance.OpenBltFlasher;
import com.rusefi.maintenance.OpenbltCallbacks;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * Hardware CI entry point that flashes the ECU via OpenBLT, to verify that an
 * OpenBLT update path produces a viable firmware image.
 *
 * Usage: java ... com.rusefi.HwCiOpenbltUpdate &lt;serial-substring&gt; &lt;update.srec&gt;
 *
 * The first argument is the chip's iSerial substring (from the STM32 UID), e.g.
 * "2B003B000A51343033393930". We never pin a fixed by-id path because the
 * firmware build and the OpenBLT bootloader build may enumerate with different
 * trailing interface suffixes (-if00 vs -if01) — but both always contain the
 * chip serial. At every step we resolve fresh against /dev/serial/by-id.
 *
 * Multiple HW CI runners may share a host machine, so we always restrict to
 * by-id entries matching this chip's serial.
 */
public class HwCiOpenbltUpdate {
    private static final File BY_ID_DIR = new File("/dev/serial/by-id");

    private static final long FIRMWARE_PORT_TIMEOUT_MS = 15_000;
    private static final long BOOTLOADER_DISCOVERY_TIMEOUT_MS = 15_000;
    private static final long POLL_INTERVAL_MS = 500;

    public static void main(String[] args) {
        boolean skipReboot = false;
        List<String> positional = new ArrayList<>();
        for (String arg : args) {
            if ("--skip-reboot".equals(arg)) {
                skipReboot = true;
            } else {
                positional.add(arg);
            }
        }
        if (positional.size() != 2) {
            System.err.println("Usage: HwCiOpenbltUpdate [--skip-reboot] <serial-substring> <update.srec>");
            System.exit(1);
        }

        String serialSubstring = positional.get(0);
        String srecPath = positional.get(1);

        try {
            run(serialSubstring, srecPath, skipReboot);
        } catch (Throwable t) {
            System.err.println("HwCiOpenbltUpdate FAILED: " + t.getMessage());
            t.printStackTrace();
            System.exit(1);
        }
    }

    private static void run(String serialSubstring, String srecPath, boolean skipReboot) throws IOException {
        if (!skipReboot) {
            System.out.println("Step 1/3: locating running firmware port for chip " + serialSubstring + "...");
            String firmwarePort = waitForAnyMatchingPort(serialSubstring, FIRMWARE_PORT_TIMEOUT_MS);
            System.out.println("Firmware port: " + firmwarePort);
            rebootToOpenblt(firmwarePort);
        } else {
            System.out.println("Step 1/3: skipped (--skip-reboot); chip is expected to already be in OpenBLT.");
        }

        System.out.println("Step 2/3: waiting for OpenBLT bootloader on chip " + serialSubstring + "...");
        String bltPort = waitForOpenbltOnAnyMatchingPort(serialSubstring);
        System.out.println("OpenBLT port: " + bltPort);

        System.out.println("Step 3/3: flashing " + srecPath + " via OpenBLT on " + bltPort);
        OpenBltFlasher flasher = OpenBltFlasher.makeSerial(bltPort, new XcpSettings(), new ConsoleOpenbltCallbacks());
        flasher.flash(srecPath);

        System.out.println("OpenBLT update completed successfully.");
    }

    /**
     * Sends CMD_REBOOT_OPENBLT to the running firmware. Inlined (rather than
     * using DfuHelper.sendDfuRebootCommand) so that IOExceptions propagate to
     * the caller — DfuHelper swallows them and merely logs, which would hide
     * the real cause behind a downstream "bootloader didn't appear" timeout.
     */
    private static void rebootToOpenblt(String port) throws IOException {
        try (IoStream stream = LinkManager.open(port)) {
            if (stream == null) {
                throw new IOException("Could not open serial port " + port);
            }
            byte[] command = BinaryProtocol.getTextCommandBytes(Fields.CMD_REBOOT_OPENBLT);
            stream.sendPacket(command);
            System.out.println("Reboot-to-OpenBLT command sent.");
        }
    }

    /** Lists /dev/serial/by-id entries whose name contains the SN substring. */
    private static List<String> findMatchingPorts(String serialSubstring) {
        List<String> matches = new ArrayList<>();
        File[] entries = BY_ID_DIR.listFiles();
        if (entries == null) {
            return matches;
        }
        for (File entry : entries) {
            if (entry.getName().contains(serialSubstring)) {
                matches.add(entry.getAbsolutePath());
            }
        }
        return matches;
    }

    /** Polls until at least one by-id entry matches the chip serial, or times out. */
    private static String waitForAnyMatchingPort(String serialSubstring, long timeoutMs) throws IOException {
        long deadline = System.currentTimeMillis() + timeoutMs;
        while (System.currentTimeMillis() < deadline) {
            List<String> matches = findMatchingPorts(serialSubstring);
            if (!matches.isEmpty()) {
                return matches.get(0);
            }
            BinaryProtocol.sleep(POLL_INTERVAL_MS);
        }
        throw new IOException("No /dev/serial/by-id entry matching '" + serialSubstring
                + "' appeared within " + timeoutMs + "ms. Visible entries: "
                + Arrays.toString(BY_ID_DIR.list()));
    }

    /**
     * Polls all by-id entries that match the chip serial, sending an XCP CONNECT
     * to each, until one responds as OpenBLT or we time out. The bootloader
     * may enumerate under a different by-id name than the firmware (different
     * interface suffix), so we don't pin a fixed path.
     */
    private static String waitForOpenbltOnAnyMatchingPort(String serialSubstring) throws IOException {
        long deadline = System.currentTimeMillis() + BOOTLOADER_DISCOVERY_TIMEOUT_MS;
        List<String> lastSeen = new ArrayList<>();
        while (System.currentTimeMillis() < deadline) {
            BinaryProtocol.sleep(POLL_INTERVAL_MS);
            lastSeen = findMatchingPorts(serialSubstring);
            for (String candidate : lastSeen) {
                if (isPortOpenblt(candidate)) {
                    return candidate;
                }
            }
        }
        throw new IOException("OpenBLT bootloader for chip '" + serialSubstring
                + "' did not respond within " + BOOTLOADER_DISCOVERY_TIMEOUT_MS
                + "ms. Last visible matching ports: " + lastSeen
                + " — either the firmware never received the reboot command, "
                + "the bootloader didn't enumerate, or the bootloader's serial "
                + "transport is broken.");
    }

    /**
     * Mirrors com.rusefi.SerialPortScanner.isPortOpenblt — sends an XCP CONNECT
     * and checks for a valid response. Duplicated here to avoid pulling the UI
     * module into the autotest classpath.
     */
    private static boolean isPortOpenblt(String port) {
        try (IoStream stream = LinkManager.open(port)) {
            if (stream == null) {
                return false;
            }

            byte[] request = new byte[3];
            request[0] = 2;             // packet length
            request[1] = (byte) 0xff;   // XCPLOADER_CMD_CONNECT
            request[2] = 0;             // connectMode

            stream.write(request);
            stream.flush();

            IncomingDataBuffer idb = stream.getDataBuffer();
            byte responseLength = idb.readByte(250);
            if (responseLength != 8) {
                return false;
            }

            byte[] response = new byte[responseLength];
            idb.waitForBytes(100, "isPortOpenblt", System.currentTimeMillis(), responseLength);
            idb.read(response);

            return response[0] == (byte) 0xFF;
        } catch (Exception e) {
            return false;
        }
    }

    private static class ConsoleOpenbltCallbacks implements OpenbltCallbacks {
        @Override
        public void log(String line) {
            System.out.println(line);
        }

        @Override
        public void updateProgress(int percent) {
            System.out.println("Progress: " + percent + "%");
        }

        @Override
        public void error(String line) {
            throw new RuntimeException(line);
        }

        @Override
        public void setPhase(String title, boolean hasProgress) {
            System.out.println("Phase: " + title);
        }
    }
}
