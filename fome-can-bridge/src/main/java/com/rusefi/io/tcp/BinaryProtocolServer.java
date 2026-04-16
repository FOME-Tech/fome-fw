package com.rusefi.io.tcp;

import com.devexperts.logging.Logging;
import com.rusefi.CompatibleFunction;
import com.rusefi.Listener;
import com.rusefi.NamedThreadFactory;
import com.rusefi.Timeouts;
import com.rusefi.binaryprotocol.IncomingDataBuffer;
import com.rusefi.binaryprotocol.IoHelper;
import com.rusefi.config.generated.Integration;
import com.rusefi.util.HexBinary;
import com.rusefi.ui.StatusConsumer;
import org.jetbrains.annotations.NotNull;

import java.io.*;
import java.net.ServerSocket;
import java.net.Socket;
import java.util.Objects;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.atomic.AtomicBoolean;

import static com.devexperts.logging.Logging.getLogging;
import static com.rusefi.config.generated.VariableRegistryValues.*;

/**
 * TCP server that accepts TunerStudio connections and relays them.
 * Slimmed down to only the proxy-essential functionality.
 */
public class BinaryProtocolServer {
    private static final Logging log = getLogging(BinaryProtocolServer.class);
    public static final String TS_OK = "\0";

    static {
        log.configureDebugEnabled(false);
    }

    private final static ConcurrentHashMap<String, ThreadFactory> THREAD_FACTORIES_BY_NAME = new ConcurrentHashMap<>();

    /**
     * Starts a TCP server socket that accepts client connections and dispatches them.
     */
    public static ServerSocketReference tcpServerSocket(int port, String threadName, CompatibleFunction<Socket, Runnable> socketRunnableFactory, Listener serverSocketCreationCallback, StatusConsumer statusConsumer) throws IOException {
        return tcpServerSocket(socketRunnableFactory, port, threadName, serverSocketCreationCallback, p -> {
            ServerSocket serverSocket = new ServerSocket(p);
            statusConsumer.logLine("ServerSocket " + p + " created. Feel free to point TS at IP Address 'localhost' port " + p);
            return serverSocket;
        });
    }

    public static ServerSocketReference tcpServerSocket(CompatibleFunction<Socket, Runnable> clientSocketRunnableFactory, int port, String threadName, Listener serverSocketCreationCallback, ServerSocketFunction nonSecureSocketFunction) throws IOException {
        ThreadFactory threadFactory = getThreadFactory(threadName);

        Objects.requireNonNull(serverSocketCreationCallback, "serverSocketCreationCallback");
        ServerSocket serverSocket = nonSecureSocketFunction.apply(port);

        ServerSocketReference holder = new ServerSocketReference(serverSocket);

        serverSocketCreationCallback.onResult(null);
        Runnable runnable = () -> {
            while (!holder.isClosed()) {
                final Socket clientSocket;
                try {
                    clientSocket = serverSocket.accept();
                } catch (IOException e) {
                    log.info("Client socket closed right away " + e);
                    continue;
                }
                log.info("Accepting binary protocol proxy port connection on " + port);
                Runnable clientRunnable = clientSocketRunnableFactory.apply(clientSocket);
                Objects.requireNonNull(clientRunnable, "Runnable for " + clientSocket);
                threadFactory.newThread(clientRunnable).start();
            }
        };
        threadFactory.newThread(runnable).start();
        return holder;
    }

    @NotNull
    public static ThreadFactory getThreadFactory(String threadName) {
        synchronized (THREAD_FACTORIES_BY_NAME) {
            ThreadFactory threadFactory = THREAD_FACTORIES_BY_NAME.get(threadName);
            if (threadFactory == null) {
                threadFactory = new NamedThreadFactory(threadName);
                THREAD_FACTORIES_BY_NAME.put(threadName, threadFactory);
            }
            return threadFactory;
        }
    }

    public static int getPacketLength(IncomingDataBuffer in, Handler protocolCommandHandler) throws IOException {
        return getPacketLength(in, protocolCommandHandler, Timeouts.BINARY_IO_TIMEOUT);
    }

    public static int getPacketLength(IncomingDataBuffer in, Handler protocolCommandHandler, int ioTimeout) throws IOException {
        byte first = in.readByte(ioTimeout);
        if (first == Integration.TS_GET_PROTOCOL_VERSION_COMMAND_F) {
            protocolCommandHandler.handle();
            return 0;
        }
        byte secondByte = in.readByte(ioTimeout);
        return IoHelper.getInt(first, secondByte);
    }

    public static Packet readPromisedBytes(IncomingDataBuffer in, int length) throws IOException {
        if (length <= 0)
            throw new IOException("Unexpected packed length " + length);
        byte[] packet = new byte[length];
        in.read(packet);
        int crc = in.readInt();
        int fromPacket = IoHelper.getCrc32(packet);
        if (crc != fromPacket)
            throw new IOException("CRC mismatch crc=" + Integer.toString(crc, 16) + " vs packet=" + Integer.toString(fromPacket, 16) + " len=" + packet.length + " data: " + HexBinary.printHexBinary(packet));
        in.onPacketArrived();
        return new Packet(packet, crc);
    }

    public interface Handler {
        void handle() throws IOException;
    }

    public static void handleProtocolCommand(Socket clientSocket) throws IOException {
        if (log.debugEnabled())
            log.debug("Got plain GetProtocol F command");
        OutputStream outputStream = clientSocket.getOutputStream();
        outputStream.write(TS_PROTOCOL.getBytes());
        outputStream.flush();
    }

    public static class Packet {
        private final byte[] packet;
        private final int crc;

        public Packet(byte[] packet, int crc) {
            this.packet = packet;
            this.crc = crc;
        }

        public byte[] getPacket() {
            return packet;
        }

        public int getCrc() {
            return crc;
        }
    }

    public static class Context {
        public int getTimeout() {
            return Timeouts.BINARY_IO_TIMEOUT;
        }
    }
}
