package com.rusefi.io.can;

import com.devexperts.logging.Logging;
import org.jetbrains.annotations.NotNull;
import tel.schich.javacan.CanChannels;
import tel.schich.javacan.CanFrame;
import tel.schich.javacan.NetworkDevice;
import tel.schich.javacan.RawCanChannel;

import java.io.IOException;

import static com.devexperts.logging.Logging.getLogging;
import static tel.schich.javacan.CanFrame.FD_NO_FLAGS;
import static tel.schich.javacan.CanSocketOptions.RECV_OWN_MSGS;

/**
 * Low-level SocketCAN send/receive helpers.
 * Uses the javacan library (tel.schich:javacan-core) which wraps Linux SocketCAN.
 *
 * The CAN interface name defaults to "can0" and can be overridden with the
 * system property {@code CAN_DEVICE_NAME}, e.g. {@code -DCAN_DEVICE_NAME=can1}.
 */
public class SocketCANHelper {
    private static final Logging log = getLogging(SocketCANHelper.class);

    public static final String CAN_DEVICE_PROPERTY = "CAN_DEVICE_NAME";
    public static final String DEFAULT_CAN_DEVICE = "can0";

    public static class CanPacket {
        public final int id;
        public final byte[] payload;

        public CanPacket(int id, byte[] payload) {
            this.id = id;
            this.payload = payload;
        }
    }

    @NotNull
    public static RawCanChannel createSocket(String deviceName) {
        final RawCanChannel socket;
        try {
            NetworkDevice canInterface = NetworkDevice.lookup(deviceName);
            socket = CanChannels.newRawChannel();
            socket.bind(canInterface);
            socket.configureBlocking(true);
            socket.setOption(RECV_OWN_MSGS, false);
            log.info("SocketCAN: opened " + deviceName);
        } catch (IOException e) {
            throw new IllegalStateException("Failed to open SocketCAN device '" + deviceName + "': " + e.getMessage(), e);
        }
        return socket;
    }

    @NotNull
    public static RawCanChannel createSocket() {
        return createSocket(System.getProperty(CAN_DEVICE_PROPERTY, DEFAULT_CAN_DEVICE));
    }

    public static void send(int id, byte[] payload, RawCanChannel channel) {
        CanFrame packet = CanFrame.create(id, FD_NO_FLAGS, payload);
        try {
            channel.write(packet);
        } catch (IOException e) {
            throw new IllegalStateException("SocketCAN send failed", e);
        }
    }

    @NotNull
    public static CanPacket read(RawCanChannel socket) throws IOException {
        CanFrame rx = socket.read();
        byte[] raw = new byte[rx.getDataLength()];
        rx.getData(raw, 0, raw.length);
        if (log.debugEnabled())
            log.debug("SocketCAN RX id=" + String.format("%X", rx.getId()) + " len=" + raw.length);
        return new CanPacket(rx.getId(), raw);
    }

    /**
     * Returns true if SocketCAN is available on this platform (Linux with javacan on classpath).
     */
    public static boolean isAvailable() {
        if (!System.getProperty("os.name", "").toLowerCase().contains("linux")) {
            return false;
        }
        try {
            Class.forName("tel.schich.javacan.RawCanChannel");
            return true;
        } catch (ClassNotFoundException e) {
            return false;
        }
    }
}
