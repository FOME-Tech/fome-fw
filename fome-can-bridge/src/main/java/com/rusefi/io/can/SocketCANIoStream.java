package com.rusefi.io.can;

import com.devexperts.logging.Logging;
import com.opensr5.io.DataListener;
import com.rusefi.binaryprotocol.IncomingDataBuffer;
import com.rusefi.uds.CanConnector;
import com.rusefi.util.HexBinary;
import com.rusefi.io.IoStream;
import com.rusefi.io.can.isotp.IsoTpCanDecoder;
import com.rusefi.io.can.isotp.IsoTpConnector;
import com.rusefi.io.serial.AbstractIoStream;
import com.rusefi.io.tcp.BinaryProtocolServer;
import org.jetbrains.annotations.Nullable;
import tel.schich.javacan.RawCanChannel;

import java.io.IOException;
import java.util.concurrent.Executor;
import java.util.concurrent.Executors;

import static com.devexperts.logging.Logging.getLogging;
import static com.rusefi.config.generated.VariableRegistryValues.*;

public class SocketCANIoStream extends AbstractIoStream {
    static Logging log = getLogging(SocketCANIoStream.class);
    private final IncomingDataBuffer dataBuffer;
    private final RawCanChannel socket;

    private int rxId;
    private int txId;

    private final IsoTpCanDecoder canDecoder = new IsoTpCanDecoder() {
        @Override
        protected void onTpFirstFrame() {
            sendCanPacket(IsoTpCanDecoder.FLOW_CONTROL);
        }
    };

    private final IsoTpConnector isoTpConnector;

    private void sendCanPacket(byte[] total) {
        if (log.debugEnabled())
            log.debug("-------sendIsoTp " + total.length + " byte(s):");

        if (log.debugEnabled())
            log.debug("Sending " + HexBinary.printHexBinary(total));

        SocketCANHelper.send(isoTpConnector.canId(), total, socket);
    }

    public SocketCANIoStream(String deviceName, int rxId, int txId) {
        this.rxId = rxId;
        this.txId = txId;
        this.isoTpConnector = new IsoTpConnector(txId) { // PC TX ID -> ECU RX ID
            @Override
            public void sendCanData(byte[] total) {
                sendCanPacket(total);
            }

            @Override
            public void receiveData() {
                readHardwarePacket(rxId);
            }
        };
        System.setProperty("CAN_DEVICE_NAME", deviceName);
        socket = SocketCANHelper.createSocket();
        // buffer could only be created once socket variable is not null due to callback
        dataBuffer = createDataBuffer();
    }

    public SocketCANIoStream() {
        this("can0", CAN_ECU_SERIAL_TX_ID, CAN_ECU_SERIAL_RX_ID);
    }

    @Nullable
    public static SocketCANIoStream create() {
        return new SocketCANIoStream();
    }

    @Override
    public void write(byte[] bytes) throws IOException {
        IsoTpConnector.sendStrategy(bytes, isoTpConnector);
    }

    @Override
    public void setInputListener(DataListener listener) {
        Executor threadExecutor = Executors.newSingleThreadExecutor(BinaryProtocolServer.getThreadFactory("SocketCAN reader"));
        threadExecutor.execute(() -> {
            while (!isClosed()) {
                readOnePacket(listener);
            }
        });
    }

    private interface SocketListener {
        void onPacket(CanConnector.CanPacket msg);
    }

    private void readOnePacket(DataListener listener) {
        readHardwarePacket((rx) -> {
            byte[] decode = canDecoder.decodePacket(rx.payload());
            if (decode != null) {
                listener.onDataArrived(decode);
            }
        });
    }

    private void readHardwarePacket(int expectedId) {
        readHardwarePacket((rx) -> {
            if (rx.id() != expectedId) {
                return;
            }
            canDecoder.decodePacket(rx.payload());
        });
    }

    private void readHardwarePacket(SocketListener listener) {
        try {
            CanConnector.CanPacket rx = SocketCANHelper.read(socket);
            listener.onPacket(rx);
        } catch (IOException e) {
            throw new IllegalStateException(e);
        }
    }

    private void readOnePacket_old(DataListener listener) {
        try {
            CanConnector.CanPacket rx = SocketCANHelper.read(socket);
            if (rx.id() != this.rxId) {
                if (log.debugEnabled())
                    log.debug("Skipping non " + String.format("%X", this.rxId) + " packet: " + String.format("%X", rx.id()));
                return;
            }
            byte[] decode = canDecoder.decodePacket(rx.payload());
            listener.onDataArrived(decode);
        } catch (IOException e) {
            throw new IllegalStateException(e);
        }
    }

    @Override
    public IncomingDataBuffer getDataBuffer() {
        return dataBuffer;
    }

    public static IoStream createStream() {
        return new SocketCANIoStream();
    }
}
