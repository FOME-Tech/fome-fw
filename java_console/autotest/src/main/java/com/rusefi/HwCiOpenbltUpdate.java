package com.rusefi;

import com.rusefi.binaryprotocol.BinaryProtocol;
import com.rusefi.binaryprotocol.IncomingDataBuffer;
import com.rusefi.config.generated.Fields;
import com.rusefi.io.IoStream;
import com.rusefi.io.LinkManager;
import com.rusefi.libopenblt.XcpSettings;
import com.rusefi.maintenance.OpenBltFlasher;
import com.rusefi.maintenance.OpenbltCallbacks;

import java.io.IOException;

/**
 * Hardware CI entry point that flashes the ECU via OpenBLT, to verify that an
 * OpenBLT update path produces a viable firmware image.
 *
 * Usage: java ... com.rusefi.HwCiOpenbltUpdate &lt;serial-by-id-path&gt; &lt;update.srec&gt;
 *
 * The first argument is a stable /dev/serial/by-id/... path identifying the
 * specific ECU. Both the firmware and the OpenBLT bootloader build use the
 * same USB descriptors (vendor "FOME", same product, iSerial derived from the
 * chip UID), so the same by-id path resolves to whichever mode the chip is
 * currently in. We always talk to that exact port — never scan all ports —
 * because more than one HW CI runner can share a host machine.
 */
public class HwCiOpenbltUpdate {
    private static final long BOOTLOADER_DISCOVERY_TIMEOUT_MS = 15_000;
    private static final long BOOTLOADER_POLL_INTERVAL_MS = 500;

    public static void main(String[] args) {
        if (args.length != 2) {
            System.err.println("Usage: HwCiOpenbltUpdate <fome-serial-port> <update.srec>");
            System.exit(1);
        }

        String fomePort = args[0];
        String srecPath = args[1];

        try {
            run(fomePort, srecPath);
        } catch (Throwable t) {
            System.err.println("HwCiOpenbltUpdate FAILED: " + t.getMessage());
            t.printStackTrace();
            System.exit(1);
        }
    }

    private static void run(String ecuPort, String srecPath) throws IOException {
        System.out.println("Step 1/3: rebooting ECU at " + ecuPort + " into OpenBLT...");
        rebootToOpenblt(ecuPort);

        System.out.println("Step 2/3: waiting for OpenBLT to respond on " + ecuPort + "...");
        waitForOpenbltOnPort(ecuPort);

        System.out.println("Step 3/3: flashing " + srecPath + " via OpenBLT on " + ecuPort);
        OpenBltFlasher flasher = OpenBltFlasher.makeSerial(ecuPort, new XcpSettings(), new ConsoleOpenbltCallbacks());
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

    /**
     * Polls the specific by-id port (the chip's stable USB serial identity) for
     * an OpenBLT XCP CONNECT response. We deliberately don't scan all ports —
     * multiple HW CI runners may share a host, and scanning could pick up a
     * different chip's bootloader.
     */
    private static void waitForOpenbltOnPort(String port) throws IOException {
        long deadline = System.currentTimeMillis() + BOOTLOADER_DISCOVERY_TIMEOUT_MS;
        while (System.currentTimeMillis() < deadline) {
            BinaryProtocol.sleep(BOOTLOADER_POLL_INTERVAL_MS);
            if (isPortOpenblt(port)) {
                return;
            }
        }
        throw new IOException("OpenBLT bootloader did not respond on " + port + " within "
                + BOOTLOADER_DISCOVERY_TIMEOUT_MS + "ms — either the firmware never received "
                + "the reboot command, the bootloader didn't enumerate, or the bootloader's "
                + "serial transport is broken.");
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
