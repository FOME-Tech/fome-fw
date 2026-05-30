package com.rusefi.io.tcp;



import java.io.Closeable;
import java.net.ServerSocket;

public class ServerSocketReference implements Closeable {
    private final ServerSocket serverSocket;
    private boolean isClosed;

    public ServerSocketReference(ServerSocket serverSocket) {
        this.serverSocket = serverSocket;
    }

    @Override
    public void close() {
        isClosed = true;
        try { serverSocket.close(); } catch (Exception ignored) {}
    }

    public boolean isClosed() {
        return isClosed;
    }
}
