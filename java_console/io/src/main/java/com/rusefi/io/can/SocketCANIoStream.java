package com.rusefi.io.can;

import com.devexperts.logging.Logging;
import com.opensr5.io.DataListener;
import com.rusefi.binaryprotocol.IncomingDataBuffer;
import com.rusefi.io.IoStream;
import com.rusefi.io.serial.AbstractIoStream;
import com.rusefi.io.tcp.BinaryProtocolServer;
import org.jetbrains.annotations.Nullable;
import tel.schich.javacan.RawCanChannel;

import java.io.IOException;
import java.util.concurrent.Executor;
import java.util.concurrent.Executors;

import static com.devexperts.logging.Logging.getLogging;

/**
 * IoStream implementation that speaks the FOME binary protocol over Linux SocketCAN,
 * using ISO-TP (ISO 15765-2) framing.
 *
 * The CAN device name defaults to "can0" and can be overridden with the system property
 * {@code CAN_DEVICE_NAME}, e.g. {@code -DCAN_DEVICE_NAME=can1}.
 *
 * CAN IDs used:
 *   TX (PC → ECU): CAN_ECU_SERIAL_RX_ID (default 0x100)
 *   RX (ECU → PC): CAN_ECU_SERIAL_TX_ID (default 0x102)
 */
public class SocketCANIoStream extends AbstractIoStream {
    private static final int CAN_ECU_SERIAL_RX_ID = 0x100;
    private static final int CAN_ECU_SERIAL_TX_ID = 0x102;

    private static final Logging log = getLogging(SocketCANIoStream.class);

    private final IncomingDataBuffer dataBuffer;
    private final RawCanChannel socket;

    private final IsoTpCanDecoder canDecoder = new IsoTpCanDecoder() {
        @Override
        protected void onTpFirstFrame() {
            sendCanPacket(IsoTpCanDecoder.FLOW_CONTROL);
        }
    };

    private final IsoTpConnector isoTpConnector = new IsoTpConnector(CAN_ECU_SERIAL_RX_ID) {
        @Override
        public void sendCanData(byte[] total) {
            sendCanPacket(total);
        }

        @Override
        public void receiveData() {
            readOnePacket(CAN_ECU_SERIAL_TX_ID);
        }
    };

    private void sendCanPacket(byte[] total) {
        if (log.debugEnabled())
            log.debug("SocketCAN TX " + total.length + " byte(s): " + IoStream.printHexBinary(total));
        SocketCANHelper.send(isoTpConnector.canId(), total, socket);
    }

    private SocketCANIoStream(String deviceName) {
        socket = SocketCANHelper.createSocket(deviceName);
        dataBuffer = createDataBuffer("SocketCAN");
    }

    /**
     * Create a stream connected to the default CAN device (can0 or CAN_DEVICE_NAME property).
     * Returns null if SocketCAN is not available on this platform.
     */
    @Nullable
    public static SocketCANIoStream create() {
        return create(System.getProperty(SocketCANHelper.CAN_DEVICE_PROPERTY, SocketCANHelper.DEFAULT_CAN_DEVICE));
    }

    /**
     * Create a stream connected to the named CAN device.
     * Returns null if SocketCAN is not available on this platform.
     */
    @Nullable
    public static SocketCANIoStream create(String deviceName) {
        if (!SocketCANHelper.isAvailable()) {
            log.info("SocketCAN not available on this platform");
            return null;
        }
        try {
            return new SocketCANIoStream(deviceName);
        } catch (Exception e) {
            log.error("Failed to create SocketCAN stream on " + deviceName + ": " + e.getMessage());
            return null;
        }
    }

    @Override
    public void write(byte[] bytes) throws IOException {
        IsoTpConnector.sendStrategy(bytes, isoTpConnector);
    }

    @Override
    public void setInputListener(DataListener listener) {
        Executor threadExecutor = Executors.newSingleThreadExecutor(
                BinaryProtocolServer.getThreadFactory("SocketCAN reader"));
        threadExecutor.execute(() -> {
            while (!isClosed()) {
                readOnePacket(listener);
            }
        });
    }

    private void readOnePacket(DataListener listener) {
        readHardwarePacket((rx) -> {
            byte[] decoded = canDecoder.decodePacket(rx.payload);
            if (decoded != null && decoded.length > 0)
                listener.onDataArrived(decoded);
        });
    }

    private interface SocketListener {
        void onPacket(SocketCANHelper.CanPacket msg);
    }

    private void readOnePacket(int expectedId) {
        readHardwarePacket((rx) -> {
            if (rx.id != expectedId) {
                return;
            }
            canDecoder.decodePacket(rx.payload);
        });
    }

    private void readHardwarePacket(SocketListener listener) {
        try {
            SocketCANHelper.CanPacket rx = SocketCANHelper.read(socket);
            listener.onPacket(rx);
        } catch (IOException e) {
            if (!isClosed())
                throw new IllegalStateException("SocketCAN read error", e);
        }
    }


    @Override
    public IncomingDataBuffer getDataBuffer() {
        return dataBuffer;
    }

    @Override
    public void close() {
        super.close();
        try {
            socket.close();
        } catch (IOException ignored) {
        }
    }
}
