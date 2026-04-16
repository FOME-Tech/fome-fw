package com.rusefi.io.can;

import com.fazecast.jSerialComm.SerialPort;
import com.fazecast.jSerialComm.SerialPortDataListener;
import com.fazecast.jSerialComm.SerialPortEvent;
import com.rusefi.binaryprotocol.IncomingDataBuffer;
import com.rusefi.io.can.isotp.IsoTpCanDecoder;
import com.rusefi.io.can.isotp.IsoTpConnector;
import com.rusefi.io.serial.AbstractIoStream;
import com.rusefi.util.HexBinary;
import java.io.IOException;
public class SlCanIoStream extends AbstractIoStream {
    private final SerialPort sp;
    private final IncomingDataBuffer dataBuffer;
    private final int rxId;
    private final int txId;
    private final StringBuilder lineBuffer = new StringBuilder();
    private java.util.concurrent.CountDownLatch waitLatch;

    public SlCanIoStream(String port, int rxId, int txId) throws IOException {
        this.rxId = rxId;
        this.txId = txId;
        try {
            this.sp = SerialPort.getCommPort(port);
        } catch (LinkageError e) {
            throw new IOException("jSerialComm native library failed to load. This can happen due to an architecture mismatch or blocked temp directory: " + e.getMessage(), e);
        }
        this.sp.setBaudRate(2000000);
        if (!this.sp.openPort()) {
            throw new IOException("Failed to open SLCAN port: " + port);
        }

        // Setup SLCAN: Close, Set Speed (S6=500k), Open
        sendString("C");
        sendString("S6");
        sendString("O");

        this.dataBuffer = createDataBuffer();
    }

    private final IsoTpCanDecoder canDecoder = new IsoTpCanDecoder() {
        @Override
        protected void onTpFirstFrame() {
            // Send flow control to the ECU's RX ID
            sendString("t" + String.format("%03X", txId) + "83000000000000000"); // 0x30 = Flow Control
        }
    };


    private void sendString(String s) {
        byte[] bytes = (s + "\r").getBytes();
        sp.writeBytes(bytes, bytes.length);
    }

    @Override
    public void write(byte[] bytes) throws IOException {
        IsoTpConnector connector = new IsoTpConnector(txId) {
            @Override
            public void sendCanData(byte[] total) {
                // Formatting as 'tiiildd...'
                String msg = String.format("t%03X%d%s",
                    canId(),
                    total.length,
                    HexUtil.asString(total));
                sendString(msg);
            }

            @Override
            public void receiveData() {
                waitLatch = new java.util.concurrent.CountDownLatch(1);
                try {
                    if (!waitLatch.await(500, java.util.concurrent.TimeUnit.MILLISECONDS)) {
                        // Timeout waiting for flow control, but we'll continue anyway
                    }
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                }
            }
        };
        IsoTpConnector.sendStrategy(bytes, connector);
    }

    @Override
    public void setInputListener(com.opensr5.io.DataListener listener) {
        sp.addDataListener(new SerialPortDataListener() {
            @Override
            public int getListeningEvents() { return SerialPort.LISTENING_EVENT_DATA_AVAILABLE; }
            @Override
            public void serialEvent(SerialPortEvent event) {
                byte[] data = new byte[sp.bytesAvailable()];
                sp.readBytes(data, data.length);
                for (byte b : data) {
                    if (b == '\r') {
                        processLine(lineBuffer.toString(), listener);
                        lineBuffer.setLength(0);
                    } else if (b != '\n') {
                        lineBuffer.append((char)b);
                    }
                }
            }
        });
    }

    private void processLine(String line, com.opensr5.io.DataListener listener) {
        if (line.startsWith("t")) { // Standard frame
            try {
                int id = Integer.parseInt(line.substring(1, 4), 16);
                int len = Integer.parseInt(line.substring(4, 5));
                if (id == rxId) {
                    String hexData = line.substring(5, 5 + (len * 2));
                    byte[] data = HexUtil.hexToBytes(hexData);
                    byte[] decoded = canDecoder.decodePacket(data);
                    if (decoded != null) {
                        if (waitLatch != null) {
                            waitLatch.countDown();
                        }
                        listener.onDataArrived(decoded);
                    }
                }
            } catch (Exception e) {
                // Malformed or different ID
            }
        }
    }

    @Override
    public IncomingDataBuffer getDataBuffer() { return dataBuffer; }

    @Override
    public void close() {
        sendString("C");
        sp.closePort();
        super.close();
    }
}
