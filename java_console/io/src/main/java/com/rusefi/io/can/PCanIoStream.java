package com.rusefi.io.can;

import com.devexperts.logging.Logging;
import com.opensr5.io.DataListener;
import com.rusefi.binaryprotocol.IncomingDataBuffer;
import com.rusefi.io.IoStream;
import com.rusefi.io.serial.AbstractIoStream;
import com.rusefi.io.tcp.BinaryProtocolServer;
import com.rusefi.ui.StatusConsumer;
import org.jetbrains.annotations.Nullable;
import peak.can.basic.PCANBasic;
import peak.can.basic.TPCANMsg;
import peak.can.basic.TPCANStatus;

import java.io.IOException;
import java.util.concurrent.Executor;
import java.util.concurrent.Executors;

import static com.devexperts.logging.Logging.getLogging;

/**
 * IoStream implementation that speaks the FOME binary protocol over a PEAK PCAN-USB adapter,
 * using ISO-TP (ISO 15765-2) framing.
 *
 * Windows only. Requires:
 *  1. PCAN driver installed (from peak-system.com)
 *  2. PCANBasic.dll + PCANBasic_JNI.dll on PATH or in the console directory
 *  3. peak-can-basic.jar on the classpath (or the real PCAN Java API JAR)
 *
 * CAN IDs used:
 *   TX (PC → ECU): CAN_ECU_SERIAL_RX_ID (default 0x100)
 *   RX (ECU → PC): CAN_ECU_SERIAL_TX_ID (default 0x102)
 */
public class PCanIoStream extends AbstractIoStream {
    private static final int CAN_ECU_SERIAL_RX_ID = 0x100;
    private static final int CAN_ECU_SERIAL_TX_ID = 0x102;

    private static final Logging log = getLogging(PCanIoStream.class);

    private final IncomingDataBuffer dataBuffer;
    private final PCANBasic can;
    private final StatusConsumer statusConsumer;

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
            log.debug("PCAN TX " + total.length + " byte(s): " + IoStream.printHexBinary(total));
        TPCANStatus status = PCanHelper.send(can, isoTpConnector.canId(), total);
        if (status != TPCANStatus.PCAN_ERROR_OK) {
            statusConsumer.logLine("PCAN write error: " + status);
        }
    }

    private PCanIoStream(PCANBasic can, StatusConsumer statusConsumer) {
        this.can = can;
        this.statusConsumer = statusConsumer;
        dataBuffer = createDataBuffer("PCAN");
    }

    /**
     * Create a PCAN stream. Returns null if the PCAN adapter is not available or fails to init.
     */
    @Nullable
    public static PCanIoStream createStream() {
        return createStream(message -> log.info(message));
    }

    @Nullable
    public static PCanIoStream createStream(StatusConsumer statusConsumer) {
        if (!PCanHelper.isAvailable()) {
            statusConsumer.logLine("PCAN library not available. Install the PEAK PCAN driver and place PCANBasic_JNI.dll on PATH.");
            return null;
        }
        PCANBasic can = PCanHelper.create();
        TPCANStatus status = PCanHelper.init(can);
        if (status != TPCANStatus.PCAN_ERROR_OK) {
            statusConsumer.logLine("PCAN init failed: " + status + ". Is the PCAN-USB adapter plugged in?");
            return null;
        }
        statusConsumer.logLine("PCAN stream opened on " + PCanHelper.CHANNEL);
        return new PCanIoStream(can, statusConsumer);
    }

    @Override
    public void write(byte[] bytes) throws IOException {
        IsoTpConnector.sendStrategy(bytes, isoTpConnector);
    }

    @Override
    public void setInputListener(DataListener listener) {
        Executor threadExecutor = Executors.newSingleThreadExecutor(
                BinaryProtocolServer.getThreadFactory("PCAN reader"));
        threadExecutor.execute(() -> {
            while (!isClosed()) {
                readOnePacket(listener);
            }
        });
    }

    private void readOnePacket(DataListener listener) {
        readHardwarePacket((rxId) -> {
            byte[] decoded = canDecoder.decodePacket(rxId.getData());
            if (decoded != null && decoded.length > 0)
                listener.onDataArrived(decoded);
        });
    }

    private interface PcanListener {
        void onPacket(TPCANMsg msg);
    }

    private void readOnePacket(int expectedId) {
        readHardwarePacket((rx) -> {
            if (rx.getID() != expectedId) {
                return;
            }
            canDecoder.decodePacket(rx.getData());
        });
    }

    private void readHardwarePacket(PcanListener listener) {
        // Allocate max-size message each call — see rusefi #4370 for why new each time
        TPCANMsg rx = new TPCANMsg(Byte.MAX_VALUE);
        TPCANStatus status = can.Read(PCanHelper.CHANNEL, rx, null);
        if (status != TPCANStatus.PCAN_ERROR_OK) {
            return; // no data yet, poll again
        }
        PCanHelper.debugPacket(rx);
        listener.onPacket(rx);
    }


    @Override
    public IncomingDataBuffer getDataBuffer() {
        return dataBuffer;
    }
}
