package com.rusefi.binaryprotocol.test;

import com.devexperts.logging.Logging;
import com.opensr5.ConfigurationImage;
import com.rusefi.binaryprotocol.BinaryProtocol;
import com.rusefi.binaryprotocol.BinaryProtocolState;
import com.rusefi.binaryprotocol.IncomingDataBuffer;
import com.rusefi.config.generated.Fields;
import com.rusefi.io.ConnectionStateListener;
import com.rusefi.io.IoStream;
import com.rusefi.io.LinkManager;
import com.rusefi.io.serial.StreamConnector;

import java.io.IOException;
import java.util.Arrays;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

import static com.devexperts.logging.Logging.getLogging;

public class SandboxCommon {
    private static final Logging log = getLogging(SandboxCommon.class);
    static {
        log.configureDebugEnabled(false);
    }

    static void runGetProtocolCommand(String prefix, IoStream tsStream) throws IOException {
        IncomingDataBuffer dataBuffer = tsStream.getDataBuffer();
        tsStream.write(new byte[]{Fields.TS_GET_PROTOCOL_VERSION_COMMAND_F});
        tsStream.flush();
        byte[] fResponse = new byte[3];
        dataBuffer.waitForBytes("hello", System.currentTimeMillis(), fResponse.length);
        dataBuffer.getData(fResponse);
        if (log.debugEnabled())
            log.debug(prefix + " Got GetProtocol F response " + IoStream.printByteArray(fResponse));
        if (fResponse[0] != '0' || fResponse[1] != '0' || fResponse[2] != '1')
            throw new IllegalStateException("Unexpected TS_COMMAND_F response " + Arrays.toString(fResponse));
    }
}
