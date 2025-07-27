package com.rusefi.io.serial;

import com.rusefi.binaryprotocol.IncomingDataBuffer;
import com.rusefi.io.IoStream;

import java.io.IOException;
import java.util.concurrent.atomic.AtomicInteger;

public abstract class AbstractIoStream implements IoStream {
    private boolean isClosed;

    private final AtomicInteger bytesOut = new AtomicInteger();

    public IncomingDataBuffer createDataBuffer(String loggingPrefix) {
        IncomingDataBuffer incomingData = new IncomingDataBuffer(loggingPrefix);
        setInputListener(incomingData::addData);
        return incomingData;
    }

    @Override
    public void close() {
        isClosed = true;
    }

    @Override
    public void write(byte[] bytes) throws IOException {
        bytesOut.addAndGet(bytes.length);
    }

    @Override
    public void flush() throws IOException {
    }

    @Override
    public boolean isClosed() {
        return isClosed;
    }
}
