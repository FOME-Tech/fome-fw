package com.rusefi;

import com.devexperts.logging.Logging;
import com.opensr5.ConfigurationImage;
import com.opensr5.ini.field.ScalarIniField;
import com.rusefi.binaryprotocol.BinaryProtocolState;
import com.rusefi.config.Field;
import com.rusefi.config.generated.Fields;
import com.rusefi.io.IoStream;
import com.rusefi.io.LinkConnector;
import com.rusefi.io.LinkManager;
import com.rusefi.io.tcp.BinaryProtocolServer;
import com.rusefi.io.tcp.TcpIoStream;
import com.rusefi.core.FileUtil;
import org.jetbrains.annotations.NotNull;

import java.io.IOException;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

import static com.devexperts.logging.Logging.getLogging;
import static com.rusefi.Timeouts.READ_IMAGE_TIMEOUT;
import static com.rusefi.config.generated.Fields.TS_FILE_VERSION;
import static com.rusefi.config.generated.Fields.TS_FILE_VERSION_OFFSET;
import static com.rusefi.io.tcp.TcpConnector.LOCALHOST;
import org.junit.Assert;

public class TestHelper {
    private static final Logging log = getLogging(TestHelper.class);
    public static final String TEST_SIGNATURE_1 = "rusEFI master.2020.07.06.frankenso_na6.2468827536";
    public static final String TEST_SIGNATURE_2 = "rusEFI master.2020.07.11.proteus_f4.1986715563";
    public static final String TEST_TOKEN_1 = "00000000-1234-1234-1234-123456789012";
    public static final String TEST_TOKEN_3 = "33333333-3333-1234-1234-123456789012";

    @NotNull
    public static ScalarIniField createIniField(Field field) {
        return new ScalarIniField(field.getName(), field.getOffset(), "", field.getType(), 1, "0");
    }

    @NotNull
    public static ConfigurationImage prepareImage(int input, ScalarIniField scalarIniField) {
        ConfigurationImage ci = new ConfigurationImage(Fields.TOTAL_CONFIG_SIZE);

        scalarIniField.setValue(ci, Integer.toString(input));
        return ci;
    }

    @NotNull
    public static BinaryProtocolServer createVirtualController(ConfigurationImage ci, int port, BinaryProtocolServer.Context context) throws IOException {
        BinaryProtocolState state = new BinaryProtocolState();
        state.setController(ci);
        byte[] currentOutputs = new byte[Fields.TS_TOTAL_OUTPUT_SIZE];
        ByteBuffer buffer = FileUtil.littleEndianWrap(currentOutputs, TS_FILE_VERSION_OFFSET, 4);
        buffer.putInt(TS_FILE_VERSION);
        state.setCurrentOutputs(currentOutputs);

        LinkManager linkManager = new LinkManager();
        linkManager.setConnector(LinkConnector.getDetachedConnector(state));
        BinaryProtocolServer server = new BinaryProtocolServer();
        server.start(linkManager, port, context);
        return server;
    }

    @NotNull
    public static IoStream connectToLocalhost(int controllerPort) {
        IoStream targetEcuSocket;
        try {
            targetEcuSocket = new TcpIoStream("[local]", new Socket(LOCALHOST, controllerPort));
        } catch (IOException e) {
            throw new IllegalStateException("Failed to connect to controller " + LOCALHOST + ":" + controllerPort);
        }
        return targetEcuSocket;
    }

    public static BinaryProtocolServer createVirtualController(int controllerPort, ConfigurationImage controllerImage, BinaryProtocolServer.Context context) throws InterruptedException {
        try {
            return createVirtualController(controllerImage, controllerPort, context);
        } catch (IOException e) {
            throw new IllegalStateException(e);
        }
    }

    public static void assertLatch(String message, CountDownLatch reconnectCounter) throws InterruptedException {
        assertLatch(message, reconnectCounter, READ_IMAGE_TIMEOUT);
    }

    public static void assertLatch(String message, CountDownLatch reconnectCounter, int timeout) throws InterruptedException {
        Assert.assertTrue(message, reconnectCounter.await(timeout, TimeUnit.MILLISECONDS));
        log.info("*******************");
        log.info(message + " is good");
        log.info("*******************");
    }

    public static void assertLatch(CountDownLatch reconnectCounter) throws InterruptedException {
        Assert.assertTrue(reconnectCounter.await(READ_IMAGE_TIMEOUT, TimeUnit.MILLISECONDS));
    }
}
