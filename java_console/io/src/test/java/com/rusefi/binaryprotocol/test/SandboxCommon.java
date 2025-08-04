package com.rusefi.binaryprotocol.test;

import com.devexperts.logging.Logging;
import com.rusefi.binaryprotocol.IncomingDataBuffer;
import com.rusefi.config.generated.Fields;
import com.rusefi.io.IoStream;

import java.io.IOException;
import java.util.Arrays;

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
