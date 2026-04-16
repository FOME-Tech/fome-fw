package com.rusefi.io.can;

import com.devexperts.logging.Logging;
import com.opensr5.io.DataListener;
import com.rusefi.binaryprotocol.IncomingDataBuffer;
import com.rusefi.config.generated.VariableRegistryValues;
import com.rusefi.io.can.isotp.DefaultFlowControl;
import com.rusefi.util.HexBinary;
import com.rusefi.io.can.isotp.IsoTpCanDecoder;
import com.rusefi.io.can.isotp.IsoTpConnector;
import com.rusefi.io.serial.AbstractIoStream;
import com.rusefi.io.serial.RateCounter;
import com.rusefi.io.tcp.BinaryProtocolServer;
import com.rusefi.ui.StatusConsumer;
import org.jetbrains.annotations.Nullable;
import peak.can.basic.*;

import java.io.IOException;
import java.util.concurrent.Executor;
import java.util.concurrent.Executors;

import static com.devexperts.logging.Logging.getLogging;
import static com.rusefi.config.generated.VariableRegistryValues.CAN_ECU_SERIAL_TX_ID;

public class PCanIoStream extends AbstractIoStream {
    private static final int INFO_SKIP_RATE = 3-00;
    static Logging log = getLogging(PCanIoStream.class);

    private final IncomingDataBuffer dataBuffer;
    private final PCANBasic can;
    private final StatusConsumer statusListener;

    private final RateCounter totalCounter = new RateCounter();
    private final RateCounter isoTpCounter = new RateCounter();
    private int rxId;
    private int txId;

    private final IsoTpCanDecoder canDecoder = new IsoTpCanDecoder() {
        @Override
        protected void onTpFirstFrame() {
            sendCanPacket(DefaultFlowControl.FLOW_CONTROL);
        }
    };

    private final IsoTpConnector isoTpConnector;
    private int logSkipRate;

    @Nullable
    public static PCanIoStream createStream() {
        return createStream(StatusConsumer.ANONYMOUS, VariableRegistryValues.CAN_ECU_SERIAL_TX_ID, VariableRegistryValues.CAN_ECU_SERIAL_RX_ID, TPCANBaudrate.PCAN_BAUD_500K);
    }

    public static PCanIoStream createStream(StatusConsumer statusListener) {
        return createStream(statusListener, VariableRegistryValues.CAN_ECU_SERIAL_TX_ID, VariableRegistryValues.CAN_ECU_SERIAL_RX_ID, TPCANBaudrate.PCAN_BAUD_500K);
    }

    public static PCanIoStream createStream(StatusConsumer statusListener, int rxId, int txId, TPCANBaudrate baudrate) {
        PCANBasic can = PCanHelper.create();
        TPCANStatus status = PCanHelper.init(can, baudrate);
        if (status != TPCANStatus.PCAN_ERROR_OK) {
            statusListener.logLine("Error initializing PCAN: " + status);
            return null;
        }
        statusListener.logLine("Creating PCAN stream...");
        return new PCanIoStream(can, statusListener, rxId, txId);
    }

    private void sendCanPacket(byte[] payLoad) {
        if (log.debugEnabled())
            log.debug("-------sendIsoTp " + payLoad.length + " byte(s):");

        if (log.debugEnabled())
            log.debug("Sending " + HexBinary.printHexBinary(payLoad));

        TPCANStatus status = PCanHelper.send(can, isoTpConnector.canId(), payLoad);
        if (status != TPCANStatus.PCAN_ERROR_OK) {
            statusListener.logLine("Unable to write the CAN message: " + status);
            System.exit(0);
        }
    }

    private PCanIoStream(PCANBasic can, StatusConsumer statusListener, int rxId, int txId) {
        this.can = can;
        this.statusListener = statusListener;
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
        dataBuffer = createDataBuffer();
    }

    @Override
    public void write(byte[] bytes) throws IOException {
        IsoTpConnector.sendStrategy(bytes, isoTpConnector);
    }

    @Override
    public void setInputListener(DataListener listener) {
        Executor threadExecutor = Executors.newSingleThreadExecutor(BinaryProtocolServer.getThreadFactory("PCAN reader"));
        threadExecutor.execute(() -> {
            while (!isClosed()) {
                readOnePacket(listener);
            }
        });
    }

    private interface PcanListener {
        void onPacket(TPCANMsg msg);
    }

    private void readOnePacket(DataListener listener) {
        readHardwarePacket((rx) -> {
            byte[] decode = canDecoder.decodePacket(rx.getData());
            if (decode != null) {
                listener.onDataArrived(decode);
            }
        });
    }

    private void readHardwarePacket(int expectedId) {
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
        if (status == TPCANStatus.PCAN_ERROR_OK) {
            totalCounter.add();
            PCanHelper.debugPacket(rx);
            listener.onPacket(rx);
        }
    }

    private void readOnePacket_old(DataListener listener) {
        // todo: can we reuse instance?
        // todo: should be? TPCANMsg rx = new TPCANMsg();
        // https://github.com/rusefi/rusefi/issues/4370 nasty work-around
        TPCANMsg rx = new TPCANMsg(Byte.MAX_VALUE);
        TPCANStatus status = can.Read(PCanHelper.CHANNEL, rx, null);
        if (status == TPCANStatus.PCAN_ERROR_OK) {
            totalCounter.add();
            if (rx.getID() != this.rxId) {
//                if (log.debugEnabled())
                logSkipRate ++;
                if (logSkipRate % INFO_SKIP_RATE == 0) {
                    PCanHelper.debugPacket(rx);
                    log.info("Skipping non " + String.format("%X", this.rxId) + " packet: " + String.format("%X", rx.getID()));
                    log.info("Total rate " + totalCounter.getCurrentRate() + ", isotp rate " + isoTpCounter.getCurrentRate());
                }
                return;
            }
            PCanHelper.debugPacket(rx);
            isoTpCounter.add();
            byte[] decode = canDecoder.decodePacket(rx.getData());
            listener.onDataArrived(decode);

            //            log.info("Decoded " + IoStream.printByteArray(decode));
        } else {
//                   log.info("Receive " + status);
        }
    }

    @Override
    public IncomingDataBuffer getDataBuffer() {
        return dataBuffer;
    }

    @Override
    public String toString() {
        return "PCanIoStream{" + PCanHelper.CHANNEL + ", " +
            "totalCounter=" + totalCounter +
            '}';
    }
}
